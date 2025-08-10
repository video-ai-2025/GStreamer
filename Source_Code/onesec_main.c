#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <windows.h> // 이 헤더는 Sleep() 함수를 위해 추가되었으므로 제거해도 됩니다.
#include <direct.h>
#include <gst/app/gstappsink.h>

#define GST_MSEC (GST_SECOND / 1000)

// 함수 선언
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data);
static GstFlowReturn on_new_sample_from_sink(GstElement *sink, gpointer data);
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);

typedef struct {
    GstElement *pipeline;
    GstElement *appsink;
    gint frame_count;
    gint frames_to_capture;
    gint capture_interval_ms;
    gboolean is_capturing_active;
    const gchar *output_dir;
    GstClockTime last_capture_time;
    GMainLoop *loop;
    GstClockTime start_time_ns; // 시작 시간 (나노초)
    GstClockTime end_time_ns;   // 종료 시간 (나노초)
    gboolean is_first_seek_done; // seek가 한 번만 실행되도록 플래그 추가
} AppData;

int main(int argc, char *argv[]) {
    AppData app_data = {NULL, NULL, 0, 0, 0, FALSE, "C:\\Users\\장성욱\\Desktop\\ws\\Soccer_project\\Gstreamer\\output_file", 0, NULL, 0, 0, FALSE};
    GstElement *source, *decodebin, *video_convert, *jpeg_encoder;
    GstBus *bus;

    gst_init(&argc, &argv);  
    _mkdir(app_data.output_dir);

    // 1. 명령줄 인자 파싱 (n, 시작 시간)
    gint fps_to_capture = 5; // 기본 초당 프레임 수
    gint start_sec = 1;      // 기본 시작 시간 (초)
    
    if (argc > 1) {
        fps_to_capture = atoi(argv[1]);
        if (fps_to_capture <= 0) {
            fps_to_capture = 5; // 유효하지 않은 값이면 기본값 사용
        }
    }
    
    if (argc > 2) {
        start_sec = atoi(argv[2]);
        if (start_sec < 0) {
            start_sec = 0; // 유효하지 않은 값이면 기본값 사용
        }
    }
    
    g_print("FPS set to: %d, Start time set to: %d seconds\n", fps_to_capture, start_sec);

    // 2. 캡처 관련 변수 설정 (몇초동안 캡쳐할지? 설정)
    app_data.start_time_ns = (GstClockTime)start_sec * GST_SECOND;
    app_data.end_time_ns = app_data.start_time_ns + (2 * GST_SECOND); // 시작 시간으로부터 2초간 캡처
    app_data.frames_to_capture = (gint)(2 * fps_to_capture);
    app_data.capture_interval_ms = (gint)(1000 / fps_to_capture);

    // 3. 엘리먼트 생성 및 파이프라인 구성
    app_data.pipeline = gst_pipeline_new("video-pipeline");
    source = gst_element_factory_make("filesrc", "file-source");
    decodebin = gst_element_factory_make("decodebin", "decoder");
    video_convert = gst_element_factory_make("videoconvert", "video-convert");
    jpeg_encoder = gst_element_factory_make("jpegenc", "jpeg-encoder");
    app_data.appsink = gst_element_factory_make("appsink", "video-sink");

    if (!app_data.pipeline || !source || !decodebin || !video_convert || !jpeg_encoder || !app_data.appsink) {
        g_printerr("Failed to create pipeline elements.\n");
        return -1;
    }

    //비디오 파일 위치 관련
    g_object_set(G_OBJECT(source), "location", "C:\\Users\\장성욱\\Desktop\\Soccernet data\\Tackle\\Tackle4.mp4", NULL);
    g_object_set(G_OBJECT(app_data.appsink), "emit-signals", TRUE, "max-buffers", 1, "drop", TRUE, "sync", FALSE, NULL);
    g_signal_connect(app_data.appsink, "new-sample", G_CALLBACK(on_new_sample_from_sink), &app_data);

    gst_bin_add_many(GST_BIN(app_data.pipeline), source, decodebin, video_convert, jpeg_encoder, app_data.appsink, NULL);

    if (!gst_element_link(source, decodebin)) {
        g_printerr("Failed to link source and decodebin.\n");
        return -1;
    }
    
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_pad_added), video_convert);
    
    if (!gst_element_link_many(video_convert, jpeg_encoder, app_data.appsink, NULL)) {
        g_printerr("Failed to link video convert, jpeg encoder, and appsink.\n");
        return -1;
    }

    app_data.loop = g_main_loop_new(NULL, FALSE);
    bus = gst_element_get_bus(app_data.pipeline);
    // bus_call의 data로 app_data 포인터를 전달하여 AppData 구조체에 접근할 수 있게 함
    gst_bus_add_watch(bus, bus_call, &app_data);
    gst_object_unref(bus);

    g_print("Playing video from start to capture frames...\n");
    gst_element_set_state(app_data.pipeline, GST_STATE_PLAYING);
    
    g_main_loop_run(app_data.loop);

    g_print("Stopping pipeline.\n");
    gst_element_set_state(app_data.pipeline, GST_STATE_NULL);
    gst_object_unref(app_data.pipeline);
    g_main_loop_unref(app_data.loop);

    return 0;
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstPad *sinkpad;
    GstElement *next_element = (GstElement *)data;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GstStructure *str = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(str);
    
    if (g_str_has_prefix(name, "video/x-raw")) {
        sinkpad = gst_element_get_static_pad(next_element, "sink");
        if (!sinkpad) {
            g_printerr("Failed to get sink pad for next element.\n");
            gst_caps_unref(caps);
            return;
        }
        
        if (GST_PAD_LINK_OK != gst_pad_link(pad, sinkpad)) {
            g_printerr("Failed to link video pad.\n");
        } else {
            g_print("Dynamically linked pad of type '%s'.\n", name);
        }
        gst_object_unref(sinkpad);
    }
    
    gst_caps_unref(caps);
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    AppData *app_data = (AppData *)data;
    
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream. Exiting.\n");
            g_main_loop_quit(app_data->loop);
            break;
            
        case GST_MESSAGE_ERROR: {
            GError *err = NULL;
            gchar *debug_info = NULL;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error: %s\n", err->message);
            g_printerr("Debugging info: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            g_main_loop_quit(app_data->loop);
            break;
        }

        case GST_MESSAGE_ASYNC_DONE: {
            // 파이프라인 상태 전환이 완료되었을 때 호출됩니다.
            if (!app_data->is_first_seek_done) {
                g_print("Pipeline is now PLAYING. Seeking to start time...\n");
                if (!gst_element_seek_simple(app_data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, app_data->start_time_ns)) {
                    g_printerr("Seek failed!\n");
                }
                app_data->is_first_seek_done = TRUE;
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}

static GstFlowReturn on_new_sample_from_sink(GstElement *sink, gpointer data) {
    AppData *app_data = (AppData *)data;
    GstSample *sample = NULL;
    
    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (sample) {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstClockTime current_time = GST_BUFFER_TIMESTAMP(buffer);
        
        // 지정된 캡처 시간 범위 내에 있는지 확인
        if (current_time >= app_data->start_time_ns && current_time < app_data->end_time_ns) {
            if (!app_data->is_capturing_active) {
                app_data->is_capturing_active = TRUE;
                g_print("Starting capture at %" GST_TIME_FORMAT "...\n", GST_TIME_ARGS(current_time));
                app_data->last_capture_time = current_time;
            }
            
            // 프레임 캡처 로직 (일정 간격마다)
            if (app_data->frame_count < app_data->frames_to_capture &&
                (current_time - app_data->last_capture_time) >= app_data->capture_interval_ms * GST_MSEC) {
                 
                 GstMapInfo map;
                 if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                    gchar filename[256];
                    sprintf(filename, "%s\\tackle_frame_%04d.jpg", app_data->output_dir, app_data->frame_count);
                    FILE *fp = fopen(filename, "wb");
                    if (fp) {
                        fwrite(map.data, 1, map.size, fp);
                        fclose(fp);
                        g_print("Captured frame saved to %s\n", filename);
                        app_data->frame_count++;
                        app_data->last_capture_time = current_time;
                    }
                    gst_buffer_unmap(buffer, &map);
                 }
            }
        } else if (current_time >= app_data->end_time_ns && app_data->is_capturing_active) {
            g_print("Capture complete. Total frames captured: %d\n", app_data->frame_count);
            g_main_loop_quit(app_data->loop);
            app_data->is_capturing_active = FALSE;
        }
        
        gst_sample_unref(sample);
    }
    return GST_FLOW_OK;
}