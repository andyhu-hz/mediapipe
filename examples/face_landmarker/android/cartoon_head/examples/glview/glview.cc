#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <sstream>

//#include <GL/glew.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "trackball.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <android/asset_manager_jni.h>
#include "glview.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#define CAM_Z (3.0f)
int width = 768;
int height = 768;
#define PI 3.1415926

const std::string shaderVetex = R"glsl(
in vec3 in_vertex;
in vec3 in_normal;
in vec2 in_texcoord;

out vec3 normal;
out vec2 texcoord;

uniform mat4 modelViewProjectionMatrix;
uniform float morphTargetBaseInfluence;
uniform float morphTargetInfluences[MORPHTARGETS_COUNT];
uniform sampler2DArray morphTargetsTexture;
uniform ivec2 morphTargetsTextureSize;
uniform int hasTargets;

vec4 getMorph(const in int vertexIndex, const in int morphTargetIndex, const in int offset ) {
  int texelIndex = vertexIndex * MORPHTARGETS_TEXTURE_STRIDE + offset;
  int y = texelIndex / morphTargetsTextureSize.x;
  int x = texelIndex - y * morphTargetsTextureSize.x;

  ivec3 morphUV = ivec3(x, y, morphTargetIndex);
  return texelFetch(morphTargetsTexture, morphUV, 0);
}

void main(void)
{
  vec3 transformed = vec3(in_vertex);
  if (hasTargets > 0) {
    transformed *= morphTargetBaseInfluence;
    for ( int i = 0; i < MORPHTARGETS_COUNT; i ++ ) {
      if (morphTargetInfluences[i] != 0.0) transformed += getMorph( gl_VertexID, i, 0 ).xyz * morphTargetInfluences[ i ];
    }
  }
  vec4 p = modelViewProjectionMatrix * vec4(transformed, 1);
  gl_Position = p;


  vec4 nn = inverse(modelViewProjectionMatrix) * vec4(normalize(in_normal), 0);
  normal = nn.xyz;

  texcoord = in_texcoord;
}
)glsl";

const std::string shaderFrag = R"glsl(
#version 300 es

uniform sampler2D diffuseTex;
uniform int uIsCurve;

in vec3 normal;
in vec2 texcoord;
layout(location = 0) out vec4 FragColor;

void main(void)
{
    if (uIsCurve > 0) {
        FragColor = texture(diffuseTex, texcoord);
    } else {
        FragColor = vec4(0.5 * normalize(normal) + 0.5, 1.0);
    }
}
)glsl";

double prevMouseX, prevMouseY;
bool mouseLeftPressed;
bool mouseMiddlePressed;
bool mouseRightPressed;
float curr_quat[4];
float prev_quat[4];

//GLFWwindow *window;

typedef struct {
  GLuint vb;
} GLBufferState;

typedef struct {
  std::vector<GLuint> diffuseTex;  // for each primitive in mesh
  GLuint morphTex;  // for morph targets texture
} GLMeshState;

typedef struct {
  std::map<std::string, GLint> attribs;
  std::map<std::string, GLint> uniforms;
} GLProgramState;

typedef struct {
  GLuint vb;     // vertex buffer
  size_t count;  // byte count
} GLCurvesState;

std::map<int, GLBufferState> gBufferState;
std::map<std::string, GLMeshState> gMeshState;
std::map<int, GLCurvesState> gCurvesMesh;
GLProgramState gGLProgramState;
GLuint gGLVao;

class BlendShapesInfluences {
public:
  std::map<std::string, float> influences;
  float matrix[16] = {0.f};
};

typedef std::map<int, std::map<std::string, int> > MorphTargets;

class MorphTargetInfo {
public:
  MorphTargetInfo() {
  }
  
  void applyFaceMesh(const BlendShapesInfluences& shapes) {
    if (targetCount == 0) {
      std::cout << "applyFaceMesh, not loaded" << std::endl;
      return;
    }
    assert(!influences.empty());
    for (auto &it : influences)
      it = 0.0f;
    
    for (const auto &it : shapes.influences) {
      const std::string name = it.first;
      float influence = it.second;
      int idx = targetMap[name];
      assert(idx >= 0 && idx < targetCount);
      influences[idx] = influence;
    }
  }
  
  int meshId = 0;
  std::string meshName;
  int primitiveIdx = 0;
  bool hasPosition = false;
  bool hasNormal = false;
  bool hasColor = false;
  int vertexDataCount = 0;
  size_t positionCount = 0;
  size_t width = 0;
  size_t height = 0;
  size_t targetCount = 0;
  std::vector<float> influences;
  std::map<std::string, int> targetMap;
};

void CheckErrors(std::string desc) {
  GLenum e = glGetError();
  if (e != GL_NO_ERROR) {
    fprintf(stderr, "OpenGL error in \"%s\": %d (%d)\n", desc.c_str(), e, e);
    exit(20);
  }
}

static std::string GetFilePathExtension(const std::string &FileName) {
  if (FileName.find_last_of(".") != std::string::npos)
    return FileName.substr(FileName.find_last_of(".") + 1);
  return "";
}

static size_t ComponentTypeByteSize(int type) {
  switch (type) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    case TINYGLTF_COMPONENT_TYPE_BYTE:
      return sizeof(char);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    case TINYGLTF_COMPONENT_TYPE_SHORT:
      return sizeof(short);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_INT:
      return sizeof(int);
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      return sizeof(float);
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
      return sizeof(double);
    default:
      return 0;
  }
}

static tinygltf::Texture* findTextureByName(tinygltf::Model &model, const std::string& name) {
  for (auto it = model.textures.begin(); it != model.textures.end(); it++) {
    if (it->name == name) {
      return &(*it);
    }
  }
  return nullptr;
}

static void SetupMeshState(tinygltf::Model &model, GLuint progId) {
  glGenVertexArrays(1, &gGLVao);
  glBindVertexArray(gGLVao);
  // Buffer
  {
    for (size_t i = 0; i < model.bufferViews.size(); i++) {
      const tinygltf::BufferView &bufferView = model.bufferViews[i];
      if (bufferView.target == 0) {
        std::cout << "WARN: bufferView.target is zero" << std::endl;
        continue;  // Unsupported bufferView.
      }

      int sparse_accessor = -1;
      for (size_t a_i = 0; a_i < model.accessors.size(); ++a_i) {
        const auto &accessor = model.accessors[a_i];
        if (accessor.bufferView == i) {
          std::cout << i << " is used by accessor " << a_i << std::endl;
          if (accessor.sparse.isSparse) {
            std::cout
                << "WARN: this bufferView has at least one sparse accessor to "
                   "it. We are going to load the data as patched by this "
                   "sparse accessor, not the original data"
                << std::endl;
            sparse_accessor = (int)a_i;
            break;
          }
        }
      }

      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
      GLBufferState state;
      glGenBuffers(1, &state.vb);
      glBindBuffer(bufferView.target, state.vb);
      std::cout << "buffer.size= " << buffer.data.size()
                << ", byteOffset = " << bufferView.byteOffset << std::endl;

      if (sparse_accessor < 0)
        glBufferData(bufferView.target, bufferView.byteLength,
                     &buffer.data.at(0) + bufferView.byteOffset,
                     GL_STATIC_DRAW);
      else {
        const auto accessor = model.accessors[sparse_accessor];
        // copy the buffer to a temporary one for sparse patching
        unsigned char *tmp_buffer = new unsigned char[bufferView.byteLength];
        memcpy(tmp_buffer, buffer.data.data() + bufferView.byteOffset,
               bufferView.byteLength);

        const size_t size_of_object_in_buffer =
            ComponentTypeByteSize(accessor.componentType);
        const size_t size_of_sparse_indices =
            ComponentTypeByteSize(accessor.sparse.indices.componentType);

        const auto &indices_buffer_view =
            model.bufferViews[accessor.sparse.indices.bufferView];
        const auto &indices_buffer = model.buffers[indices_buffer_view.buffer];

        const auto &values_buffer_view =
            model.bufferViews[accessor.sparse.values.bufferView];
        const auto &values_buffer = model.buffers[values_buffer_view.buffer];

        for (size_t sparse_index = 0; sparse_index < accessor.sparse.count;
             ++sparse_index) {
          int index = 0;
          // std::cout << "accessor.sparse.indices.componentType = " <<
          // accessor.sparse.indices.componentType << std::endl;
          switch (accessor.sparse.indices.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
              index = (int)*(
                  unsigned char *)(indices_buffer.data.data() +
                                   indices_buffer_view.byteOffset +
                                   accessor.sparse.indices.byteOffset +
                                   (sparse_index * size_of_sparse_indices));
              break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
              index = (int)*(
                  unsigned short *)(indices_buffer.data.data() +
                                    indices_buffer_view.byteOffset +
                                    accessor.sparse.indices.byteOffset +
                                    (sparse_index * size_of_sparse_indices));
              break;
            case TINYGLTF_COMPONENT_TYPE_INT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
              index = (int)*(
                  unsigned int *)(indices_buffer.data.data() +
                                  indices_buffer_view.byteOffset +
                                  accessor.sparse.indices.byteOffset +
                                  (sparse_index * size_of_sparse_indices));
              break;
          }
          std::cout << "updating sparse data at index  : " << index
                    << std::endl;
          // index is now the target of the sparse index to patch in
          const unsigned char *read_from =
              values_buffer.data.data() +
              (values_buffer_view.byteOffset +
               accessor.sparse.values.byteOffset) +
              (sparse_index * (size_of_object_in_buffer * accessor.type));

          /*
          std::cout << ((float*)read_from)[0] << "\n";
          std::cout << ((float*)read_from)[1] << "\n";
          std::cout << ((float*)read_from)[2] << "\n";
          */

          unsigned char *write_to =
              tmp_buffer + index * (size_of_object_in_buffer * accessor.type);

          memcpy(write_to, read_from, size_of_object_in_buffer * accessor.type);
        }

        // debug:
        /*for(size_t p = 0; p < bufferView.byteLength/sizeof(float); p++)
        {
          float* b = (float*)tmp_buffer;
          std::cout << "modified_buffer [" << p << "] = " << b[p] << '\n';
        }*/

        glBufferData(bufferView.target, bufferView.byteLength, tmp_buffer,
                     GL_STATIC_DRAW);
        delete[] tmp_buffer;
      }
      glBindBuffer(bufferView.target, 0);

      gBufferState[(int)i] = state;
    }
  }

#if 1  // TODO(syoyo): Implement
	// Texture
	{
		for (size_t i = 0; i < model.meshes.size(); i++) {
			const tinygltf::Mesh &mesh = model.meshes[i];

			gMeshState[mesh.name].diffuseTex.resize(mesh.primitives.size());
			for (size_t primId = 0; primId < mesh.primitives.size(); primId++) {
				const tinygltf::Primitive &primitive = mesh.primitives[primId];

				gMeshState[mesh.name].diffuseTex[primId] = 0;

				if (primitive.material < 0) {
					continue;
				}
				tinygltf::Material &mat = model.materials[primitive.material];
				 printf("material.name = %s\n", mat.name.c_str());
				if (mat.values.find("baseColorTexture") != mat.values.end()) {
					std::string diffuseTexName = mat.values["baseColorTexture"].string_value;
          tinygltf::Texture* texIt = findTextureByName(model, diffuseTexName);
					if (texIt) {
						if (texIt->source >= 0) {
              tinygltf::Image &image = model.images[texIt->source];
							GLuint texId;
							glGenTextures(1, &texId);
              GLenum texTarget = GL_TEXTURE_2D; //tex.target
              glBindTexture(texTarget, texId);
							glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
							glTexParameterf(texTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							glTexParameterf(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

							// Ignore Texture.fomat.
							GLenum format = GL_RGBA;
							if (image.component == 3) {
								format = GL_RGB;
							}
							glTexImage2D(texTarget, 0, format, image.width,
									image.height, 0, format, GL_UNSIGNED_BYTE,
									&image.image.at(0));
              glGenerateMipmap(texTarget);

							CheckErrors("texImage2D");
							glBindTexture(texTarget, 0);

							printf("TexId = %d\n", texId);
							gMeshState[mesh.name].diffuseTex[primId] = texId;
						}
					}
				}
			}
		}
	}
#endif

  glUseProgram(progId);
  GLint vtloc = glGetAttribLocation(progId, "in_vertex");
  GLint nrmloc = glGetAttribLocation(progId, "in_normal");
  GLint uvloc = glGetAttribLocation(progId, "in_texcoord");

  GLint diffuseTexLoc = glGetUniformLocation(progId, "diffuseTex");
  GLint isCurvesLoc = glGetUniformLocation(progId, "uIsCurve");
  GLint morphTargetBaseInfluence = glGetUniformLocation(progId, "morphTargetBaseInfluence");
//  GLint fragColorLoc = glGetAttribLocation(progId, "FragColor");

  gGLProgramState.attribs["POSITION"] = vtloc;
  gGLProgramState.attribs["NORMAL"] = nrmloc;
  gGLProgramState.attribs["TEXCOORD_0"] = uvloc;
  gGLProgramState.uniforms["diffuseTex"] = diffuseTexLoc;
  gGLProgramState.uniforms["isCurvesLoc"] = isCurvesLoc;
  gGLProgramState.uniforms["morphTargetBaseInfluence"] = morphTargetBaseInfluence;
};

#if 0  // TODO(syoyo): Implement
// Setup curves geometry extension
static void SetupCurvesState(tinygltf::Scene &scene, GLuint progId) {
	// Find curves primitive.
	{
		std::map<std::string, tinygltf::Mesh>::const_iterator it(
				scene.meshes.begin());
		std::map<std::string, tinygltf::Mesh>::const_iterator itEnd(
				scene.meshes.end());

		for (; it != itEnd; it++) {
			const tinygltf::Mesh &mesh = it->second;

			// Currently we only support one primitive per mesh.
			if (mesh.primitives.size() > 1) {
				continue;
			}

			for (size_t primId = 0; primId < mesh.primitives.size(); primId++) {
				const tinygltf::Primitive &primitive = mesh.primitives[primId];

				gMeshState[mesh.name].diffuseTex[primId] = 0;

				if (primitive.material.empty()) {
					continue;
				}

				bool has_curves = false;
				if (primitive.extras.IsObject()) {
					if (primitive.extras.Has("ext_mode")) {
						const tinygltf::Value::Object &o =
							primitive.extras.Get<tinygltf::Value::Object>();
						const tinygltf::Value &ext_mode = o.find("ext_mode")->second;

						if (ext_mode.IsString()) {
							const std::string &str = ext_mode.Get<std::string>();
							if (str.compare("curves") == 0) {
								has_curves = true;
							}
						}
					}
				}

				if (!has_curves) {
					continue;
				}

				// Construct curves buffer
				const tinygltf::Accessor &vtx_accessor =
					scene.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::Accessor &nverts_accessor =
					scene.accessors[primitive.attributes.find("NVERTS")->second];
				const tinygltf::BufferView &vtx_bufferView =
					scene.bufferViews[vtx_accessor.bufferView];
				const tinygltf::BufferView &nverts_bufferView =
					scene.bufferViews[nverts_accessor.bufferView];
				const tinygltf::Buffer &vtx_buffer =
					scene.buffers[vtx_bufferView.buffer];
				const tinygltf::Buffer &nverts_buffer =
					scene.buffers[nverts_bufferView.buffer];

				// std::cout << "vtx_bufferView = " << vtx_accessor.bufferView <<
				// std::endl;
				// std::cout << "nverts_bufferView = " << nverts_accessor.bufferView <<
				// std::endl;
				// std::cout << "vtx_buffer.size = " << vtx_buffer.data.size() <<
				// std::endl;
				// std::cout << "nverts_buffer.size = " << nverts_buffer.data.size() <<
				// std::endl;

				const int *nverts =
					reinterpret_cast<const int *>(nverts_buffer.data.data());
				const float *vtx =
					reinterpret_cast<const float *>(vtx_buffer.data.data());

				// Convert to GL_LINES data.
				std::vector<float> line_pts;
				size_t vtx_offset = 0;
				for (int k = 0; k < static_cast<int>(nverts_accessor.count); k++) {
					for (int n = 0; n < nverts[k] - 1; n++) {

						line_pts.push_back(vtx[3 * (vtx_offset + n) + 0]);
						line_pts.push_back(vtx[3 * (vtx_offset + n) + 1]);
						line_pts.push_back(vtx[3 * (vtx_offset + n) + 2]);

						line_pts.push_back(vtx[3 * (vtx_offset + n + 1) + 0]);
						line_pts.push_back(vtx[3 * (vtx_offset + n + 1) + 1]);
						line_pts.push_back(vtx[3 * (vtx_offset + n + 1) + 2]);

						// std::cout << "p0 " << vtx[3 * (vtx_offset + n) + 0] << ", "
						//                  << vtx[3 * (vtx_offset + n) + 1] << ", "
						//                  << vtx[3 * (vtx_offset + n) + 2] << std::endl;

						// std::cout << "p1 " << vtx[3 * (vtx_offset + n+1) + 0] << ", "
						//                  << vtx[3 * (vtx_offset + n+1) + 1] << ", "
						//                  << vtx[3 * (vtx_offset + n+1) + 2] << std::endl;
					}

					vtx_offset += nverts[k];
				}

				GLCurvesState state;
				glGenBuffers(1, &state.vb);
				glBindBuffer(GL_ARRAY_BUFFER, state.vb);
				glBufferData(GL_ARRAY_BUFFER, line_pts.size() * sizeof(float),
						line_pts.data(), GL_STATIC_DRAW);
				glBindBuffer(GL_ARRAY_BUFFER, 0);

				state.count = line_pts.size() / 3;
				gCurvesMesh[mesh.name] = state;

				// Material
				tinygltf::Material &mat = scene.materials[primitive.material];
				// printf("material.name = %s\n", mat.name.c_str());
				if (mat.values.find("diffuse") != mat.values.end()) {
					std::string diffuseTexName = mat.values["diffuse"].string_value;
					if (scene.textures.find(diffuseTexName) != scene.textures.end()) {
						tinygltf::Texture &tex = scene.textures[diffuseTexName];
						if (scene.images.find(tex.source) != scene.images.end()) {
							tinygltf::Image &image = scene.images[tex.source];
							GLuint texId;
							glGenTextures(1, &texId);
							glBindTexture(tex.target, texId);
							glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
							glTexParameterf(tex.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							glTexParameterf(tex.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

							// Ignore Texture.fomat.
							GLenum format = GL_RGBA;
							if (image.component == 3) {
								format = GL_RGB;
							}
							glTexImage2D(tex.target, 0, tex.internalFormat, image.width,
									image.height, 0, format, tex.type,
									&image.image.at(0));

							CheckErrors("texImage2D");
							glBindTexture(tex.target, 0);

							printf("TexId = %d\n", texId);
							gMeshState[mesh.name].diffuseTex[primId] = texId;
						}
					}
				}
			}
		}
	}

	glUseProgram(progId);
	GLint vtloc = glGetAttribLocation(progId, "in_vertex");
	GLint nrmloc = glGetAttribLocation(progId, "in_normal");
	GLint uvloc = glGetAttribLocation(progId, "in_texcoord");

	GLint diffuseTexLoc = glGetUniformLocation(progId, "diffuseTex");
	GLint isCurvesLoc = glGetUniformLocation(progId, "uIsCurves");

	gGLProgramState.attribs["POSITION"] = vtloc;
	gGLProgramState.attribs["NORMAL"] = nrmloc;
	gGLProgramState.attribs["TEXCOORD_0"] = uvloc;
	gGLProgramState.uniforms["diffuseTex"] = diffuseTexLoc;
	gGLProgramState.uniforms["uIsCurves"] = isCurvesLoc;
};
#endif

static void DrawMesh(tinygltf::Model &model, const tinygltf::Mesh &mesh, const MorphTargetInfo& morphInfo) {
  if (gGLProgramState.uniforms["diffuseTex"] >= 0) {
    glUniform1i(gGLProgramState.uniforms["diffuseTex"], 0);  // TEXTURE0
  }

  if (gGLProgramState.uniforms["isCurvesLoc"] >= 0) {
    glUniform1i(gGLProgramState.uniforms["isCurvesLoc"], 1);
  }
  
  if (gGLProgramState.uniforms["morphTargetBaseInfluence"] >= 0) {
    glUniform1f(gGLProgramState.uniforms["morphTargetBaseInfluence"], 1.f);
  }

  if (gGLProgramState.uniforms["morphTargetInfluences"] >= 0) {
    GLfloat *v = new GLfloat[morphInfo.targetCount];
    memset(v, 0, sizeof(GLfloat) * morphInfo.targetCount);
    if (morphInfo.meshName == mesh.name) {
      for (int i = 0; i < morphInfo.targetCount; i++) {
        v[i] = morphInfo.influences[i];
      }
    }
    glUniform1fv(gGLProgramState.uniforms["morphTargetInfluences"], morphInfo.targetCount, v);
    delete [] v;
  }

  if (gGLProgramState.uniforms["morphTargetsTextureSize"] >= 0) {
    glUniform2i(gGLProgramState.uniforms["morphTargetsTextureSize"], morphInfo.width, morphInfo.height);
  }
  
  int hasTargets = 0;
  if (gGLProgramState.uniforms["morphTargetsTexture"] >= 0) {
    glUniform1i(gGLProgramState.uniforms["morphTargetsTexture"], 1);  // TEXTURE1
    if (morphInfo.meshName == mesh.name) {
      hasTargets = 1;
    }
  }
  if (gGLProgramState.uniforms["hasTargets"] >= 0) {
    glUniform1i(gGLProgramState.uniforms["hasTargets"], hasTargets);
  }

  for (size_t i = 0; i < mesh.primitives.size(); i++) {
    const tinygltf::Primitive &primitive = mesh.primitives[i];

    if (primitive.indices < 0) return;

    // Assume TEXTURE_2D target for the texture object.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gMeshState[mesh.name].diffuseTex[i]);

    std::map<std::string, int>::const_iterator it(primitive.attributes.begin());
    std::map<std::string, int>::const_iterator itEnd(
        primitive.attributes.end());

    for (; it != itEnd; it++) {
      assert(it->second >= 0);
      const tinygltf::Accessor &accessor = model.accessors[it->second];
      glBindBuffer(GL_ARRAY_BUFFER, gBufferState[accessor.bufferView].vb);
      CheckErrors("bind buffer");
      int size = 1;
      if (accessor.type == TINYGLTF_TYPE_SCALAR) {
        size = 1;
      } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
        size = 2;
      } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
        size = 3;
      } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
        size = 4;
      } else {
        assert(0);
      }
      // it->first would be "POSITION", "NORMAL", "TEXCOORD_0", ...
      if ((it->first.compare("POSITION") == 0) ||
          (it->first.compare("NORMAL") == 0) ||
          (it->first.compare("TEXCOORD_0") == 0)) {
        if (gGLProgramState.attribs[it->first] >= 0) {
          // Compute byteStride from Accessor + BufferView combination.
          int byteStride =
              accessor.ByteStride(model.bufferViews[accessor.bufferView]);
          assert(byteStride != -1);
          glVertexAttribPointer(gGLProgramState.attribs[it->first], size,
                                accessor.componentType,
                                accessor.normalized ? GL_TRUE : GL_FALSE,
                                byteStride, BUFFER_OFFSET(accessor.byteOffset));
          CheckErrors("vertex attrib pointer");
          glEnableVertexAttribArray(gGLProgramState.attribs[it->first]);
          CheckErrors("enable vertex attrib array");
        }
      }
    }

    const tinygltf::Accessor &indexAccessor =
        model.accessors[primitive.indices];
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                 gBufferState[indexAccessor.bufferView].vb);
    CheckErrors("bind buffer");
    int mode = -1;
    if (primitive.mode == TINYGLTF_MODE_TRIANGLES) {
      mode = GL_TRIANGLES;
    } else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP) {
      mode = GL_TRIANGLE_STRIP;
    } else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN) {
      mode = GL_TRIANGLE_FAN;
    } else if (primitive.mode == TINYGLTF_MODE_POINTS) {
      mode = GL_POINTS;
    } else if (primitive.mode == TINYGLTF_MODE_LINE) {
      mode = GL_LINES;
    } else if (primitive.mode == TINYGLTF_MODE_LINE_LOOP) {
      mode = GL_LINE_LOOP;
    } else {
      assert(0);
    }
    glDrawElements(mode, (GLsizei)indexAccessor.count, indexAccessor.componentType,
                   BUFFER_OFFSET(indexAccessor.byteOffset));
    CheckErrors("draw elements");

    {
      std::map<std::string, int>::const_iterator it(
          primitive.attributes.begin());
      std::map<std::string, int>::const_iterator itEnd(
          primitive.attributes.end());

      for (; it != itEnd; it++) {
        if ((it->first.compare("POSITION") == 0) ||
            (it->first.compare("NORMAL") == 0) ||
            (it->first.compare("TEXCOORD_0") == 0)) {
          if (gGLProgramState.attribs[it->first] >= 0) {
            glDisableVertexAttribArray(gGLProgramState.attribs[it->first]);
          }
        }
      }
    }
  }
}

#if 0  // TODO(syoyo): Implement
static void DrawCurves(tinygltf::Scene &scene, const tinygltf::Mesh &mesh) {
	(void)scene;

	if (gCurvesMesh.find(mesh.name) == gCurvesMesh.end()) {
		return;
	}

	if (gGLProgramState.uniforms["isCurvesLoc"] >= 0) {
		glUniform1i(gGLProgramState.uniforms["isCurvesLoc"], 1);
	}

	GLCurvesState &state = gCurvesMesh[mesh.name];

	if (gGLProgramState.attribs["POSITION"] >= 0) {
		glBindBuffer(GL_ARRAY_BUFFER, state.vb);
		glVertexAttribPointer(gGLProgramState.attribs["POSITION"], 3, GL_FLOAT,
				GL_FALSE, /* stride */ 0, BUFFER_OFFSET(0));
		CheckErrors("curve: vertex attrib pointer");
		glEnableVertexAttribArray(gGLProgramState.attribs["POSITION"]);
		CheckErrors("curve: enable vertex attrib array");
	}

	glDrawArrays(GL_LINES, 0, state.count);

	if (gGLProgramState.attribs["POSITION"] >= 0) {
		glDisableVertexAttribArray(gGLProgramState.attribs["POSITION"]);
	}
}
#endif

static void QuatToAngleAxis(const std::vector<double> quaternion,
			    double &outAngleDegrees,
			    double *axis) {
  double qx = quaternion[0];
  double qy = quaternion[1];
  double qz = quaternion[2];
  double qw = quaternion[3];
  
  double angleRadians = 2 * acos(qw);
  if (angleRadians == 0.0) {
    outAngleDegrees = 0.0;
    axis[0] = 0.0;
    axis[1] = 0.0;
    axis[2] = 1.0;
    return;
  }

  double denom = sqrt(1-qw*qw);
  outAngleDegrees = angleRadians * 180.0 / M_PI;
  axis[0] = qx / denom;
  axis[1] = qy / denom;
  axis[2] = qz / denom;
}

// Hierarchically draw nodes
static void DrawNode(tinygltf::Model &model, const tinygltf::Node &node, const MorphTargetInfo& morphInfo) {
  // Apply xform
  // std::cout << "node " << node.name << ", Meshes " << node.meshes.size() <<
  // std::endl;

  // std::cout << it->first << std::endl;
  // FIXME(syoyo): Refactor.
  // DrawCurves(scene, it->second);
  if (node.mesh > -1) {
    assert(node.mesh < model.meshes.size());
    DrawMesh(model, model.meshes[node.mesh], morphInfo);
  }

  // Draw child nodes.
  for (size_t i = 0; i < node.children.size(); i++) {
    assert(node.children[i] < model.nodes.size());
    DrawNode(model, model.nodes[node.children[i]], morphInfo);
  }
}

static void DrawModel(tinygltf::Model &model, MorphTargetInfo morphInfo) {
#if 0
	std::map<std::string, tinygltf::Mesh>::const_iterator it(scene.meshes.begin());
	std::map<std::string, tinygltf::Mesh>::const_iterator itEnd(scene.meshes.end());

	for (; it != itEnd; it++) {
		DrawMesh(scene, it->second);
		DrawCurves(scene, it->second);
	}
#else
  // If the glTF asset has at least one scene, and doesn't define a default one
  // just show the first one we can find
  assert(model.scenes.size() > 0);
  int scene_to_display = model.defaultScene > -1 ? model.defaultScene : 0;
  const tinygltf::Scene &scene = model.scenes[scene_to_display];
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    DrawNode(model, model.nodes[scene.nodes[i]], morphInfo);
  }
#endif
}

static void PrintNodes(const tinygltf::Scene &scene) {
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    std::cout << "node.name : " << scene.nodes[i] << std::endl;
  }
}

MorphTargets findAllMorphTargets(const tinygltf::Model& model) {
  MorphTargets targets;
  for (int i = 0; i < model.meshes.size(); i++) {
    const tinygltf::Mesh& mesh = model.meshes[i];
    std::map<std::string, int> blendShapes;
    if (mesh.extras.IsObject()) {
      const tinygltf::Value& targetNames = mesh.extras.Get("targetNames");
      size_t len = targetNames.ArrayLen();
      if (len > 0) {
        for (int i = 0; i < len; i++) {
          const tinygltf::Value& target = targetNames.Get(i);
          std::string targetName = target.GetStringVal();
          if (!targetName.empty()) {
            blendShapes[targetName] = i;
          }
        }
      }
    }
    if (!blendShapes.empty())
      targets[i] = blendShapes;
  }
  return targets;
}

MorphTargetInfo buildMorphTargetTexture(tinygltf::Model& model, const MorphTargets& morphTargets) {
  MorphTargetInfo ret;
  assert(!morphTargets.empty());
  ret.targetMap = morphTargets.begin()->second;
  ret.meshId = morphTargets.begin()->first;
  ret.targetCount = morphTargets.begin()->second.size();
  std::cout << "buildMorphTargetTexture, meshId=" << ret.meshId << std::endl;
  tinygltf::Mesh& mesh = model.meshes[ret.meshId];
  ret.meshName = mesh.name;
  for (int i = 0; i < mesh.primitives.size(); i++) {
    const tinygltf::Primitive & primitive = mesh.primitives[i];
    if (primitive.targets.size() == ret.targetCount) {
      ret.primitiveIdx = i;
    }
  }

  const tinygltf::Primitive& primitive = mesh.primitives[ret.primitiveIdx];
  for (int i = 0; i < primitive.targets.size(); i++) {
    for (auto &it : primitive.targets[i]) {
      if (it.first == "POSITION") {
        ret.hasPosition = true;
        auto &accessor = model.accessors[it.second];
        ret.positionCount = accessor.count;
      }
      if (it.first == "NORMAL") ret.hasNormal = true;
      if (it.first == "COLOR_0") ret.hasColor = true;
    }
    if (ret.hasPosition && ret.hasNormal && ret.hasColor) break;
  }
  if (ret.hasPosition) ret.vertexDataCount = 1;
  if (ret.hasNormal) ret.vertexDataCount = 2;
  if (ret.hasColor) ret.vertexDataCount = 3;

  GLint maxTextureSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  CheckErrors("get max texture size");
  std::cout << "max texture size is: " << maxTextureSize << std::endl;
  
  ret.width = ret.positionCount * ret.vertexDataCount;
  ret.height = 1;
  ret.influences.resize(ret.targetCount);

  if (ret.width > maxTextureSize) {
    ret.height = ceil(ret.width / maxTextureSize);
    ret.width = maxTextureSize;
  }
  return ret;
}

static void SetupMorphTextures(tinygltf::Model &model, const MorphTargetInfo& morphInfo, GLuint progId) {
  const tinygltf::Mesh& mesh = model.meshes[morphInfo.meshId];
  const tinygltf::Primitive& primitive = mesh.primitives[morphInfo.primitiveIdx];
  size_t layerCount = primitive.targets.size();
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA32F, morphInfo.width, morphInfo.height, layerCount);
  CheckErrors("allocate morph textures");
  
  float_t *dstBuffer = new float[morphInfo.width * morphInfo.height * 4];
  for (int i = 0; i < layerCount; i++) {
    for (auto &it : primitive.targets[i]) {
      const auto &accessor = model.accessors[it.second];
      const auto &bufferView = model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
      
      //std::cout << "layer=" << i << ", component type=" << accessor.componentType << std::endl;
      assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT); //5126
      size_t srcComponentCount = 3;
      switch (accessor.type) {
        case TINYGLTF_TYPE_VEC4:
          srcComponentCount = 4;
          break;
        case TINYGLTF_TYPE_VEC3:
          srcComponentCount = 3;
          break;
        default:
          assert(false);
          break;
      }
      const float *srcBuffer = (const float*)(&(buffer.data.at(0)) + bufferView.byteOffset);

      size_t positionCount = accessor.count;
      for (int j = 0; j < positionCount; j ++ ) {
        const size_t stride = j * morphInfo.vertexDataCount * 4;
        const size_t srcStride = j * srcComponentCount;
        if (it.first == "POSITION") {
          dstBuffer[stride + 0] = srcBuffer[srcStride + 0];
          dstBuffer[stride + 1] = srcBuffer[srcStride + 1];
          dstBuffer[stride + 2] = srcBuffer[srcStride + 2];
          dstBuffer[stride + 3] = 0;
        }
        if (it.first == "NORMAL") {
          dstBuffer[stride + 4] = srcBuffer[srcStride + 0];
          dstBuffer[stride + 5] = srcBuffer[srcStride + 1];
          dstBuffer[stride + 6] = srcBuffer[srcStride + 2];
          dstBuffer[stride + 7] = 0;
        }
        if (it.first == "COLOR_0") {
          dstBuffer[stride + 8] = srcBuffer[srcStride + 0];
          dstBuffer[stride + 9] = srcBuffer[srcStride + 1];
          dstBuffer[stride + 10] = srcBuffer[srcStride + 2];
          dstBuffer[stride + 11] = srcComponentCount == 4 ? srcBuffer[srcStride + 3] : 1;
        }
      }
    }
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, morphInfo.width, morphInfo.height, 1, GL_RGBA, GL_FLOAT, dstBuffer);
  }
  gMeshState[mesh.name].morphTex = texture;
  delete [] dstBuffer;
  
  GLint morphTargetsTextureSize = glGetUniformLocation(progId, "morphTargetsTextureSize");
  GLint morphTargetInfluences = glGetUniformLocation(progId, "morphTargetInfluences");
  GLint morphTargetsTexture = glGetUniformLocation(progId, "morphTargetsTexture");
  GLint hasTargets = glGetUniformLocation(progId, "hasTargets");
  

  gGLProgramState.uniforms["morphTargetsTextureSize"] = morphTargetsTextureSize;
  gGLProgramState.uniforms["morphTargetInfluences"] = morphTargetInfluences;
  gGLProgramState.uniforms["morphTargetsTexture"] = morphTargetsTexture;
  gGLProgramState.uniforms["hasTargets"] = hasTargets;

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gMeshState[mesh.name].morphTex);
}

MorphTargetInfo morphTargetInfo;
GLuint MatrixID;
bool FaceShaderInited = false;

int InitFace() {
  trackball(curr_quat, 0, 0, 0, 0);

  auto morphTargets = findAllMorphTargets(model);
  morphTargetInfo = buildMorphTargetTexture(model, morphTargets);
  size_t morphTargetCount = 0;
  if (!morphTargets.empty())
    morphTargetCount = morphTargets.begin()->second.size();
  std::stringstream shaderDefs;
  shaderDefs << "#version 300 es" << std::endl;
  shaderDefs << "#define MORPHTARGETS_COUNT " << morphTargetCount << std::endl;
  shaderDefs << "#define MORPHTARGETS_TEXTURE_STRIDE " << morphTargetInfo.vertexDataCount << std::endl;

  GLuint vertId = 0, fragId = 0, progId = 0;
  if (false == LoadShader(GL_VERTEX_SHADER, vertId, shaderDefs.str() + shaderVetex)) {
    return -1;
  }
  CheckErrors("load vert shader");

  if (false == LoadShader(GL_FRAGMENT_SHADER, fragId, shaderFrag)) {
    return -1;
  }
  CheckErrors("load frag shader");

  if (false == LinkShader(progId, vertId, fragId)) {
    return -1;
  }

  CheckErrors("link");

  {
    // At least `in_vertex` should be used in the shader.
    GLint vtxLoc = glGetAttribLocation(progId, "in_vertex");
    if (vtxLoc < 0) {
      printf("vertex loc not found.\n");
      exit(-1);
    }
  }

  glUseProgram(progId);
  CheckErrors("useProgram");

  SetupMeshState(model, progId);
  CheckErrors("SetupMeshState");
  SetupMorphTextures(model, morphTargetInfo, progId);
  CheckErrors("SetupMorphTextures");
  // SetupCurvesState(model, progId);
  MatrixID = glGetUniformLocation(progId, "modelViewProjectionMatrix");

  std::cout << "# of meshes = " << model.meshes.size() << std::endl;
  return 1;
}

int UpdateFace() {
  if(!FaceShaderInited) {
    InitFace();
    FaceShaderInited = true;
  }

  BlendShapesInfluences shapes;
  glClearColor(0.1f, 0.2f, 0.3f, 0.5f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  //
  GLfloat mat[4][4];
  build_rotmatrix(mat, curr_quat);

  std::ostringstream oss;
  oss << "Float Array: [";
  for(int i =0; i<BlendShapeKeyList.size(); i++) {
    oss << BlendShapeKeyList[i];
    oss << " = ";
    oss << BlendShapesValue[i];
    if (i < 51) {
      oss << ", ";
    }
    shapes.influences[BlendShapeKeyList[i]] = BlendShapesValue[i];
  }
  oss << "]";
  //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "BlendShapedValue %s",oss.str().c_str() );

  GLfloat  resultMatrix[4][4] = {0};
  mat[3][3] = -1.0f;

  matrixMultiply(FacialTransformationMatrix, mat, resultMatrix);

  for(int i =0; i<4; i++) {
    std::ostringstream oss2;
    oss2 << "Mat Array: [";
    for (int j = 0; j < 4; j++) {
      oss2 << mat[i][j];
      if(j < 3) {
        oss2 << ", ";
      }
    }
    oss2 << " ]";
    //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "%s",oss2.str().c_str() );
  }
  //__android_log_print(ANDROID_LOG_INFO, "AndyTest","" );
  resultMatrix[3][0] = 0.0f;
  resultMatrix[3][1] = 0.0f;
  resultMatrix[3][2] = 0.0f;
  resultMatrix[3][3] = 1.0f;

  glUniformMatrix4fv(MatrixID, 1, GL_FALSE, &resultMatrix[0][0]);

    if(BlendShapeKeyList.size() > 0) {
      std::ostringstream oss;
      oss << "Float Array: [";
        for(int i =0; i<BlendShapeKeyList.size(); i++) {
          oss << BlendShapeKeyList[i];
          oss << " = ";
          oss << BlendShapesValue[i];
          if (i < 51) {
            oss << ", ";
          }
          shapes.influences[BlendShapeKeyList[i]] = BlendShapesValue[i];
        }
      oss << "]";
      //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "BlendShapedValue %s",oss.str().c_str() );
    }
    morphTargetInfo.applyFaceMesh(shapes);
    DrawModel(model, morphTargetInfo);
    glFlush();
  return 1;
}
