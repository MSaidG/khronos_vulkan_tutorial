package com.vulkan.mobile

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.view.WindowManager
// import com.google.androidgamesdk.GameActivity
import android.widget.TextView
import com.vulkan.mobile.databinding.ActivityMainBinding
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Example of a call to a native method
        binding.sampleText.text = stringFromJNI()

        // Keep the screen on while the app is running
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    /**
     * A native method that is implemented by the 'mobile' native library,
     * which is packaged with this application.
     */
    external fun stringFromJNI(): String

    companion object {
        // Used to load the 'mobile' library on application startup.
        init {
            System.loadLibrary("mobile")
        }
    }
}