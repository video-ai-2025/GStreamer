# GStreamer
# 컴파일 방법(X64 Native Tool Command Prompt)
cl.exe onesec_main.c /Fe:onesec_main.exe /I"C:\Program Files\gstreamer\1.0\msvc_x86_64\include\gstreamer-1.0" /I"C:\Program Files\gstreamer\1.0\msvc_x86_64\include" /I"C:\Program Files\gstreamer\1.0\msvc_x86_64\include\glib-2.0" /I"C:\Program Files\gstreamer\1.0\msvc_x86_64\lib\glib-2.0\include" /link /LIBPATH:"C:\Program Files\gstreamer\1.0\msvc_x86_64\lib" gstreamer-1.0.lib gobject-2.0.lib glib-2.0.lib intl.lib gstapp-1.0.lib
# 실행 
.\onesec_main.exe (FPS) (Start time)  Ex) .\onesec_main.exe (10) (2)
