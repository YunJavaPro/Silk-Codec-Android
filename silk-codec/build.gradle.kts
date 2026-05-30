plugins {
    id("com.android.library")
    id("maven-publish")
}

android {
    namespace = "me.yun.silk"
    compileSdk = 36

    defaultConfig {
        minSdk = 26
        ndk {
            abiFilters.addAll(listOf("arm64-v8a"))
        }
    }

    externalNativeBuild {
        ndkBuild {
            path = file("src/main/jni/Android.mk")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }
}

publishing {
    publications {
        create<MavenPublication>("release") {
            groupId = "com.github.YunJavaPro"
            artifactId = "silk-codec"
            version = "1.0.1"
            afterEvaluate {
                from(components["release"])
            }
        }
    }
}
