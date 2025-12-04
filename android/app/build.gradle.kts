import org.gradle.internal.declarativedsl.parsing.main

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.vulkan.mobile"
    compileSdk {
        version = release(36)
    }

    defaultConfig {
        applicationId = "com.vulkan.mobile"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
        prefab = true
    }

    ndkVersion = "29.0.14206865"

    sourceSets {
        getByName("main") {
            assets.srcDirs(
                // Debug shader outputs
                ".externalNativeBuild/cmake/debug/arm64-v8a/shaders",
                ".externalNativeBuild/cmake/debug/armeabi-v7a/shaders",
                ".externalNativeBuild/cmake/debug/x86/shaders",
                ".externalNativeBuild/cmake/debug/x86_64/shaders",

                // Release shader outputs
                ".externalNativeBuild/cmake/release/arm64-v8a/shaders",
                ".externalNativeBuild/cmake/release/armeabi-v7a/shaders",
                ".externalNativeBuild/cmake/release/x86/shaders",
                ".externalNativeBuild/cmake/release/x86_64/shaders"
            )
        }
    }
    buildToolsVersion = "36.1.0"

}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.games.activity)
    implementation(libs.androidx.games.controller)
    implementation(libs.androidx.games.frame.pacing)
    implementation(libs.androidx.games.performance.tuner)
    //implementation("com.google.android")
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}