plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
android {
    namespace = "dev.ultrascale.mobile"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "dev.ultrascale.mobile"
        minSdk = 26
        targetSdk = 35
        versionCode = 7
        versionName = "0.6.1-layer-lab"
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++17", "-fvisibility=hidden") }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    buildFeatures { compose = true; aidl = true; buildConfig = true; prefab = true }
}
dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.activity:activity-compose:1.10.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    // Native inline-hook backend (API 35-compatible release) for code already loaded in an authorized target process.
    implementation("com.bytedance.android:shadowhook:1.1.1")
    // Complementary PLT hook backend; ByteHook 1.1.1 pairs with ShadowHook 1.1.1.
    implementation("com.bytedance:bytehook:1.1.1")
    // Static APK/DEX/manifest parser for engine fingerprinting.
    implementation("net.dongliu:apk-parser:2.6.10")
}
