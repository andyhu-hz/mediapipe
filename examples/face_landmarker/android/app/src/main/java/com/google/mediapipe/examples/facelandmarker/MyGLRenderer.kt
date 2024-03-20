package com.google.mediapipe.examples.facelandmarker

import android.opengl.GLES30
import android.opengl.Matrix
import com.google.mediapipe.examples.facelandmarker.widget.GLTextureView.Renderer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MyGLRenderer : Renderer {

    external fun NativeSurfaceCreate();
    external fun NativeSurfaceChange(width:Int, height:Int);
    external fun NativeOnFrame();
    private companion object {
        const val FOVY = 60.0f
        const val NEAR = 0.1f
        const val FAR = 100.0f
    }

    private val projectionMatrix = FloatArray(16)

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        // Initialize OpenGL settings
        GLES30.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        // Update the viewport and perspective projection matrix
        GLES30.glViewport(0, 0, width, height)
        val aspectRatio = width.toFloat() / height.toFloat()
        Matrix.perspectiveM(projectionMatrix, 0, FOVY, aspectRatio, NEAR, FAR)
        NativeSurfaceChange(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        // Render the scene
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        //Log.e("Andy Test", "MyGlRender onDrawFrame")
        // Perform OpenGL ES 3.0 rendering here
        NativeOnFrame()
    }

    override fun onSurfaceDestroyed(gl: GL10?) {}
}
