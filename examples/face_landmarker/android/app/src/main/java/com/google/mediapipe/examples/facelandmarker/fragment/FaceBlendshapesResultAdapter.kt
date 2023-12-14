/*
 * Copyright 2023 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *             http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.mediapipe.examples.facelandmarker.fragment

import android.os.Build
import android.util.Log
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.annotation.RequiresApi
import androidx.recyclerview.widget.RecyclerView
import com.google.mediapipe.examples.facelandmarker.OverlayView
import com.google.mediapipe.examples.facelandmarker.databinding.FaceBlendshapesResultBinding
import com.google.mediapipe.tasks.components.containers.Category
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult
import com.google.protobuf.LazyStringArrayList
import java.util.Optional
import kotlin.jvm.internal.Intrinsics


class FaceBlendshapesResultAdapter :
    RecyclerView.Adapter<FaceBlendshapesResultAdapter.ViewHolder>() {
    companion object {
        private const val NO_VALUE = "--"
    }
    external fun NativeSetBlendshapeAndMatrixed(blendshapes: FloatArray, matrix: FloatArray)
    external fun NativeSetBlendshapeKey(blendshapeKey: MutableList<String>);

    private var categories: MutableList<Category?> = MutableList(52) { null }

    val blendshapeKey: MutableList<String> = MutableList(52) { "" }
    val blendshapeValue: FloatArray = FloatArray(52){0.0f}
    val FacialTransformationMatrix: FloatArray = FloatArray(16){0.0f}

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    fun updateResults(faceLandmarkerResult: FaceLandmarkerResult? = null) {
        categories = MutableList(52) { null }
        if (faceLandmarkerResult != null) {
            val sortedCategories = faceLandmarkerResult.faceBlendshapes().get()[0].sortedBy { -it.score() }
            val min = kotlin.math.min(sortedCategories.size, categories.size)

            for (i in 0 until min) {
                categories[i] = sortedCategories[i]
                blendshapeKey[i] = sortedCategories[i].categoryName();
                blendshapeValue[i] = sortedCategories[i].score();
            }

            val matrixes = faceLandmarkerResult.facialTransformationMatrixes();
            if (!matrixes.isEmpty()) {
                for(i in 0 until 15) {
                    FacialTransformationMatrix[i] = matrixes.get()[0][i];
                }
            }

            //val aString = "Float Array: [${blendshapeKey.joinToString(", ")}]"
            //Log.i("AndyTest", "Java -- Blendshape key : "  + aString )

            //val arrayString = "Float Array: [${blendshapeValue.joinToString(", ")}]"
            //Log.i("AndyTest", "Java -- Blendshape Value : "  + arrayString )

            //val array2String = "Float Array: [${FacialTransformationMatrix.joinToString(", ")}]"
            //Log.e("AndyTest", "Java -- Blendshape Matrix : "  + array2String )

            NativeSetBlendshapeKey(blendshapeKey);
            NativeSetBlendshapeAndMatrixed(blendshapeValue, FacialTransformationMatrix)
        }
    }

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): ViewHolder {
        val binding = FaceBlendshapesResultBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return ViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        categories[position].let { category ->
            holder.bind(category?.categoryName(), category?.score())
        }
    }

    override fun getItemCount(): Int = categories.size

    inner class ViewHolder(private val binding: FaceBlendshapesResultBinding) :
        RecyclerView.ViewHolder(binding.root) {

        fun bind(label: String?, score: Float?) {
            with(binding) {
                tvLabel.text = label ?: NO_VALUE
                tvScore.text = if (score != null) String.format(
                    "%.2f",
                    score
                ) else NO_VALUE
            }
        }
    }


}
