//
// Created by Andy Hu on 2023/12/12.
//

#ifndef MP_FACE_LANDMARKER_GLVIEW_H
#define MP_FACE_LANDMARKER_GLVIEW_H

#include <jni.h>
#include <vector>
#include <string>
#include "tiny_gltf.h"
#include <android/log.h>

tinygltf::Model model;
std::map<std::string, float> blendShapeMap;

std::vector<std::string> BlendShapeKeyList(52);
GLfloat BlendShapesValue[52] = {0.0f};
GLfloat FacialTransformationMatrix[4][4] = {0.0f};

#ifdef __cplusplus
extern "C" {
#endif
int InitFace();
int UpdateFace();
#ifdef __cplusplus
}
#endif

void updateSize(int width, int height) {
    glViewport(0, -250, 1100, 1100);
}


extern "C" {
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_fragment_FaceBlendshapesResultAdapter_NativeSetBlendshapeKey(JNIEnv *env, jobject thiz, jobject stringList) {
    jclass arrayListClass = env->GetObjectClass(stringList);

    jmethodID sizeMethod = env->GetMethodID(arrayListClass, "size", "()I");
    jint size = env->CallIntMethod(stringList, sizeMethod);

    jmethodID getMethod = env->GetMethodID(arrayListClass, "get", "(I)Ljava/lang/Object;");

    BlendShapeKeyList.clear();
    for (int i = 0; i < size; ++i) {
        jstring jString = (jstring)env->CallObjectMethod(stringList, getMethod, i);
        const char *cString = env->GetStringUTFChars(jString, nullptr);
        std::string cppString(cString);
        BlendShapeKeyList.push_back(cppString);
        env->ReleaseStringUTFChars(jString, cString);
        env->DeleteLocalRef(jString);
    }
    env->DeleteLocalRef(arrayListClass);
    //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "NativeSetBlendshapeKey succeed");
}
}  // extern "C"

extern "C" {
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_fragment_FaceBlendshapesResultAdapter_NativeSetBlendshapeAndMatrixed(JNIEnv *env, jobject thiz, jfloatArray blendShapeArray, jfloatArray matrixArray) {
    jfloat *blednshapesArrayElements = env->GetFloatArrayElements(blendShapeArray, nullptr);
    int index = 0;
    for (int i = 0; i < 52; ++i) {
        BlendShapesValue[i] = blednshapesArrayElements[index++];
    }

    env->ReleaseFloatArrayElements(blendShapeArray, blednshapesArrayElements, 0);

    //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "NativeSetBlendshapeValue succeed");

    jfloat *matrixArrayElements = env->GetFloatArrayElements(matrixArray, nullptr);
    index = 0;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            FacialTransformationMatrix[col][row] = matrixArrayElements[index++];
        }
    }
    env->ReleaseFloatArrayElements(matrixArray, matrixArrayElements, 0);
    //__android_log_print(ANDROID_LOG_INFO, "AndyTest", "NativeSetBlendshapeMatrix succeed");

}
}  // extern "C"

extern "C"
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_OverlayView_NativeSetAssets(JNIEnv *env, jobject thiz, jobject assetManager) {
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    AAsset *asset = AAssetManager_open(mgr, "raccoon_head.glb", AASSET_MODE_BUFFER);
    if (asset != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, "AndyTest", "Read GLB file success");
        int64_t fileSize = AAsset_getLength(asset);
        std::vector<char> buffer(fileSize);
        AAsset_read(asset, buffer.data(), fileSize);
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        unsigned char* ucharPtr = reinterpret_cast<unsigned char*>(buffer.data());
        bool ret = false;
        ret = loader.LoadBinaryFromMemory(&model, &err, &warn, ucharPtr, fileSize, "");
        if (!ret) {
            __android_log_print(ANDROID_LOG_ERROR, "AndyTest", "Read GLB file failed");
        }
        AAsset_close(asset);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "AndyTest", "Read GLB file failed");
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_MyGLRenderer_NativeSurfaceCreate(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, "AndyTest", "JNI--NativeSurfaceCreate");
    //todo
    EGLContext currentContext = eglGetCurrentContext();
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_MyGLRenderer_NativeSurfaceChange(JNIEnv *env, jobject thiz,
                                                                                   jint width,
                                                                                   jint height) {
    __android_log_print(ANDROID_LOG_INFO, "AndyTest", "JNI--NativeSurfaceChange");
    updateSize(width, height);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_google_mediapipe_examples_facelandmarker_MyGLRenderer_NativeOnFrame(JNIEnv *env, jobject thiz) {
//__android_log_print(ANDROID_LOG_INFO, "AndyTest", "JNI--NativeOnFrame");
    UpdateFace();
    //drawTriangle();
}

bool LoadShader(GLenum shaderType,  // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER(or maybe GL_COMPUTE_SHADER)
                GLuint &shader, const std::string& shaderCode) {
    GLint val = 0;

    // free old shader/program
    if (shader != 0) {
        glDeleteShader(shader);
    }

    std::vector<GLchar> srcbuf(shaderCode.size() + 1);
    memcpy(&srcbuf[0], shaderCode.c_str(), shaderCode.size());
    srcbuf[shaderCode.size()] = 0;

    const GLchar *srcs[1];
    srcs[0] = &srcbuf.at(0);

    shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, srcs, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &val);
    if (val != GL_TRUE) {
        char log[4096];
        GLsizei msglen;
        glGetShaderInfoLog(shader, 4096, &msglen, log);
        printf("%s\n", log);
        // assert(val == GL_TRUE && "failed to compile shader");
        printf("ERR: Failed to load or compile shader [ %d ]\n", shaderType);
        return false;
    }

    printf("Load shader [ %d ] OK\n", shaderType);
    return true;
}

bool LinkShader(GLuint &prog, GLuint &vertShader, GLuint &fragShader) {
    GLint val = 0;

    if (prog != 0) {
        glDeleteProgram(prog);
    }

    prog = glCreateProgram();

    glAttachShader(prog, vertShader);
    glAttachShader(prog, fragShader);
    glLinkProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &val);
    assert(val == GL_TRUE && "failed to link shader");

    printf("Link shader OK\n");

    return true;
}

#endif //MP_FACE_LANDMARKER_GLVIEW_H
