# The page calls these through window.AndroidMidi
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
