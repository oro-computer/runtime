#!/usr/bin/env bash
# vim: set syntax=bash:

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

cd "$root" || exit $?

rm -f "$root/build.gradle" "$root/gradle.properties"

## build.gradle
cat > "$root/build.gradle" << GRADLE
buildscript {
  repositories {
    google()
    mavenCentral()
  }

  dependencies {
    classpath 'com.android.tools.build:gradle:9.3.2'
  }
}

allprojects {
  repositories {
    google()
    mavenCentral()
  }
}

apply plugin: 'com.android.application'

android {
  namespace "oro.runtime"
  compileSdk 37
  ndkVersion "29.0.14206865"
  flavorDimensions "default"

  compileOptions {
    sourceCompatibility JavaVersion.VERSION_17
    targetCompatibility JavaVersion.VERSION_17
  }

  defaultConfig {
    applicationId "oro.runtime"
    minSdk 26
    targetSdk 37
    versionCode 1
    versionName "0.0.1"
  }

  sourceSets {
    main {
      kotlin {
        srcDir "src"
      }
    }
  }
}

dependencies {
  implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3'
  implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3'
  implementation 'androidx.fragment:fragment-ktx:1.7.1'
  implementation 'androidx.lifecycle:lifecycle-process:2.7.0'
  implementation 'androidx.appcompat:appcompat:1.6.1'
  implementation 'androidx.core:core-ktx:1.13.0'
  implementation 'androidx.webkit:webkit:1.9.0'
}
GRADLE

## gradle.properties
cat > "$root/gradle.properties" << GRADLE
org.gradle.jvmargs=-Xmx2048m
org.gradle.parallel=true

android.useAndroidX=true
kotlin.code.style=official
GRADLE

gradle wrapper && "$root/gradlew" androidDependencies
