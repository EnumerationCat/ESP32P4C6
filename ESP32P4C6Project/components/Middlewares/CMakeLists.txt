set(src_dirs    wifi_manager/wifi_manager.c
				wifi_manager/websocket_server.c
				wifi_manager/ap_wifi.c
				iot/onenet_mqtt.c
				iot/onenet_token.c
				iot/onenet_dm.c
				iot/onenet_ota.c
				# LVGL sources
				# examples/porting (adapted to the BSP MIPI-DSI panel and GT9xxx touch)
				LVGL/GUI/lvgl/examples/porting/lv_port_disp.c
				LVGL/GUI/lvgl/examples/porting/lv_port_indev.c

				# src/core
				LVGL/GUI/lvgl/src/core/lv_disp.c
				LVGL/GUI/lvgl/src/core/lv_event.c
				LVGL/GUI/lvgl/src/core/lv_group.c
				LVGL/GUI/lvgl/src/core/lv_indev_scroll.c
				LVGL/GUI/lvgl/src/core/lv_indev.c
				LVGL/GUI/lvgl/src/core/lv_obj_class.c
				LVGL/GUI/lvgl/src/core/lv_obj_draw.c
				LVGL/GUI/lvgl/src/core/lv_obj_pos.c
				LVGL/GUI/lvgl/src/core/lv_obj_scroll.c
				LVGL/GUI/lvgl/src/core/lv_obj_style_gen.c
				LVGL/GUI/lvgl/src/core/lv_obj_style.c
				LVGL/GUI/lvgl/src/core/lv_obj_tree.c
				LVGL/GUI/lvgl/src/core/lv_obj.c
				LVGL/GUI/lvgl/src/core/lv_refr.c
				LVGL/GUI/lvgl/src/core/lv_theme.c

				# src/draw (excluding nxp_pxp, nxp_vglite, sdl, stm32_dma2d)
				LVGL/GUI/lvgl/src/draw/lv_draw_arc.c
				LVGL/GUI/lvgl/src/draw/lv_draw_img.c
				LVGL/GUI/lvgl/src/draw/lv_draw_label.c
				LVGL/GUI/lvgl/src/draw/lv_draw_line.c
				LVGL/GUI/lvgl/src/draw/lv_draw_mask.c
				LVGL/GUI/lvgl/src/draw/lv_draw_rect.c
				LVGL/GUI/lvgl/src/draw/lv_draw_triangle.c
				LVGL/GUI/lvgl/src/draw/lv_draw.c
				LVGL/GUI/lvgl/src/draw/lv_img_buf.c
				LVGL/GUI/lvgl/src/draw/lv_img_cache.c
				LVGL/GUI/lvgl/src/draw/lv_img_decoder.c

				# src/draw/sw
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_arc.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_blend.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_dither.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_gradient.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_img.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_letter.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_line.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_polygon.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_rect.c
				LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw.c
				
			

				# src/extra
				LVGL/GUI/lvgl/src/extra/layouts/flex/lv_flex.c
				LVGL/GUI/lvgl/src/extra/layouts/grid/lv_grid.c
				
				LVGL/GUI/lvgl/src/extra/libs/bmp/lv_bmp.c
				LVGL/GUI/lvgl/src/extra/libs/ffmpeg/lv_ffmpeg.c
				LVGL/GUI/lvgl/src/extra/libs/freetype/lv_freetype.c

				LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_fatfs.c
				LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_posix.c
				LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_stdio.c
				LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_win32.c

				LVGL/GUI/lvgl/src/extra/libs/gif/gifdec.c
				LVGL/GUI/lvgl/src/extra/libs/gif/lv_gif.c
				LVGL/GUI/lvgl/src/extra/libs/png/lodepng.c
				LVGL/GUI/lvgl/src/extra/libs/png/lv_png.c
				LVGL/GUI/lvgl/src/extra/libs/qrcode/lv_qrcode.c
				LVGL/GUI/lvgl/src/extra/libs/qrcode/qrcodegen.c

				LVGL/GUI/lvgl/src/extra/libs/rlottie/lv_rlottie.c

				LVGL/GUI/lvgl/src/extra/libs/sjpg/lv_sjpg.c
				LVGL/GUI/lvgl/src/extra/libs/sjpg/tjpgd.c


				LVGL/GUI/lvgl/src/extra/others/gridnav/lv_gridnav.c
				LVGL/GUI/lvgl/src/extra/others/monkey/lv_monkey.c
				LVGL/GUI/lvgl/src/extra/others/snapshot/lv_snapshot.c

				LVGL/GUI/lvgl/src/extra/themes/basic/lv_theme_basic.c
				LVGL/GUI/lvgl/src/extra/themes/default/lv_theme_default.c
				LVGL/GUI/lvgl/src/extra/themes/mono/lv_theme_mono.c

				LVGL/GUI/lvgl/src/extra/widgets/animimg/lv_animimg.c
				LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar_header_arrow.c
				LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar_header_dropdown.c
				LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar.c
				LVGL/GUI/lvgl/src/extra/widgets/chart/lv_chart.c
				LVGL/GUI/lvgl/src/extra/widgets/colorwheel/lv_colorwheel.c
				LVGL/GUI/lvgl/src/extra/widgets/imgbtn/lv_imgbtn.c
				LVGL/GUI/lvgl/src/extra/widgets/keyboard/lv_keyboard.c
				LVGL/GUI/lvgl/src/extra/widgets/led/lv_led.c
				LVGL/GUI/lvgl/src/extra/widgets/list/lv_list.c
				LVGL/GUI/lvgl/src/extra/widgets/menu/lv_menu.c
				LVGL/GUI/lvgl/src/extra/widgets/meter/lv_meter.c
				LVGL/GUI/lvgl/src/extra/widgets/msgbox/lv_msgbox.c
				LVGL/GUI/lvgl/src/extra/widgets/span/lv_span.c
				LVGL/GUI/lvgl/src/extra/widgets/spinbox/lv_spinbox.c
				LVGL/GUI/lvgl/src/extra/widgets/spinner/lv_spinner.c
				LVGL/GUI/lvgl/src/extra/widgets/tabview/lv_tabview.c
				LVGL/GUI/lvgl/src/extra/widgets/tileview/lv_tileview.c
				LVGL/GUI/lvgl/src/extra/widgets/win/lv_win.c
				LVGL/GUI/lvgl/src/extra/lv_extra.c

				# src/font
				
				LVGL/GUI/lvgl/src/font/lv_font_dejavu_16_persian_hebrew.c
				LVGL/GUI/lvgl/src/font/lv_font_fmt_txt.c
				LVGL/GUI/lvgl/src/font/lv_font_loader.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_8.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_10.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_12_subpx.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_12.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_14.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_16.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_18.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_20.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_22.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_24.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_26.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_28_compressed.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_28.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_30.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_32.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_34.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_36.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_38.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_40.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_42.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_44.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_46.c
				LVGL/GUI/lvgl/src/font/lv_font_montserrat_48.c
				LVGL/GUI/lvgl/src/font/lv_font_simsun_16_cjk.c
				LVGL/GUI/lvgl/src/font/lv_font_unscii_16.c
				LVGL/GUI/lvgl/src/font/lv_font_unscii_8.c
				LVGL/GUI/lvgl/src/font/lv_font.c



				#src/gpu
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_arc.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_bg.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_composite.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_img.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_label.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_line.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_mask.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_polygon.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_rect.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_stack_blur.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_texture_cache.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_utils.c
				LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl.c
				LVGL/GUI/lvgl/src/draw/stm32_dma2d/lv_gpu_stm32_dma2d.c


			
				# src/hal
				
				LVGL/GUI/lvgl/src/hal/lv_hal_disp.c
				LVGL/GUI/lvgl/src/hal/lv_hal_indev.c
				LVGL/GUI/lvgl/src/hal/lv_hal_tick.c

				# src/misc
				LVGL/GUI/lvgl/src/misc/lv_anim_timeline.c
				LVGL/GUI/lvgl/src/misc/lv_anim.c
				LVGL/GUI/lvgl/src/misc/lv_area.c
				LVGL/GUI/lvgl/src/misc/lv_async.c
				LVGL/GUI/lvgl/src/misc/lv_bidi.c
				LVGL/GUI/lvgl/src/misc/lv_color.c
				LVGL/GUI/lvgl/src/misc/lv_fs.c
				LVGL/GUI/lvgl/src/misc/lv_gc.c
				LVGL/GUI/lvgl/src/misc/lv_ll.c
				LVGL/GUI/lvgl/src/misc/lv_log.c
				LVGL/GUI/lvgl/src/misc/lv_lru.c
				LVGL/GUI/lvgl/src/misc/lv_math.c
				LVGL/GUI/lvgl/src/misc/lv_mem.c
				LVGL/GUI/lvgl/src/misc/lv_printf.c
				LVGL/GUI/lvgl/src/misc/lv_style_gen.c
				LVGL/GUI/lvgl/src/misc/lv_style.c
				LVGL/GUI/lvgl/src/misc/lv_templ.c
				LVGL/GUI/lvgl/src/misc/lv_timer.c
				LVGL/GUI/lvgl/src/misc/lv_tlsf.c
				LVGL/GUI/lvgl/src/misc/lv_txt_ap.c
				LVGL/GUI/lvgl/src/misc/lv_txt.c
				LVGL/GUI/lvgl/src/misc/lv_utils.c

				# src/widgets
				LVGL/GUI/lvgl/src/widgets/lv_arc.c
				LVGL/GUI/lvgl/src/widgets/lv_bar.c
				LVGL/GUI/lvgl/src/widgets/lv_btn.c
				LVGL/GUI/lvgl/src/widgets/lv_btnmatrix.c
				LVGL/GUI/lvgl/src/widgets/lv_canvas.c
				LVGL/GUI/lvgl/src/widgets/lv_checkbox.c
				LVGL/GUI/lvgl/src/widgets/lv_dropdown.c
				LVGL/GUI/lvgl/src/widgets/lv_img.c
				LVGL/GUI/lvgl/src/widgets/lv_label.c
				LVGL/GUI/lvgl/src/widgets/lv_line.c
				LVGL/GUI/lvgl/src/widgets/lv_objx_templ.c

				LVGL/GUI/lvgl/src/widgets/lv_roller.c
				LVGL/GUI/lvgl/src/widgets/lv_slider.c
				LVGL/GUI/lvgl/src/widgets/lv_switch.c
				LVGL/GUI/lvgl/src/widgets/lv_table.c
				LVGL/GUI/lvgl/src/widgets/lv_textarea.c



				LVGL/GUI_APP/demos/stress/lv_demo_stress.c

				LVGL/GUI_APP/demos/widgets/lv_demo_widgets.c
				LVGL/GUI_APP/demos/widgets/assets/img_clothes.c
				LVGL/GUI_APP/demos/widgets/assets/img_demo_widgets_avatar.c
				LVGL/GUI_APP/demos/widgets/assets/img_lvgl_logo.c
				

				
				LVGL/GUI_APP/demos/music/lv_demo_music.c
				LVGL/GUI_APP/demos/music/lv_demo_music_list.c
				LVGL/GUI_APP/demos/music/lv_demo_music_main.c
				
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_corner_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_list_pause.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_list_pause_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_list_play.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_list_play_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_loop.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_loop_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_next.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_next_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_pause.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_pause_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_play.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_play_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_prev.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_prev_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_rnd.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_btn_rnd_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_corner_left.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_corner_left_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_corner_right.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_corner_right_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_1.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_1_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_2.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_2_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_3.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_cover_3_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_1.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_1_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_2.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_2_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_3.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_3_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_4.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_icon_4_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_list_border.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_list_border_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_logo.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_slider_knob.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_slider_knob_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_wave_bottom.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_wave_bottom_large.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_wave_top.c
				LVGL/GUI_APP/demos/music/assets/img_lv_demo_music_wave_top_large.c



				LVGL/GUI_APP/demos/keypad_encoder/lv_demo_keypad_encoder.c

				LVGL/GUI_APP/demos/benchmark/lv_demo_benchmark.c
				LVGL/GUI_APP/demos/benchmark/assets/img_benchmark_cogwheel_alpha16.c
				LVGL/GUI_APP/demos/benchmark/assets/img_benchmark_cogwheel_argb.c
				LVGL/GUI_APP/demos/benchmark/assets/img_benchmark_cogwheel_chroma_keyed.c
				LVGL/GUI_APP/demos/benchmark/assets/img_benchmark_cogwheel_indexed16.c
				LVGL/GUI_APP/demos/benchmark/assets/img_benchmark_cogwheel_rgb.c
				LVGL/GUI_APP/demos/benchmark/assets/lv_font_bechmark_montserrat_12_compr_az.c.c
				LVGL/GUI_APP/demos/benchmark/assets/lv_font_bechmark_montserrat_16_compr_az.c.c
				LVGL/GUI_APP/demos/benchmark/assets/lv_font_bechmark_montserrat_28_compr_az.c.c

				LVGL/GUI_APP/user_home/user_home.c
				LVGL/GUI_APP/app_shell/app_shell.c
				LVGL/GUI_APP/app_shell/command.c
)

set(include_dirs wifi_manager
				 iot
				 LVGL/GUI
				 LVGL/GUI/lvgl
				 LVGL/GUI/lvgl/examples/porting
				 LVGL/GUI/lvgl/src
				 LVGL/GUI/lvgl/src/core
				 LVGL/GUI/lvgl/src/draw
				 LVGL/GUI/lvgl/src/extra
				 LVGL/GUI/lvgl/src/font
				 LVGL/GUI/lvgl/src/gpu
				 LVGL/GUI/lvgl/src/draw/sdl
				 LVGL/GUI/lvgl/src/draw/stm32_dma2d
				 LVGL/GUI/lvgl/src/hal
				 LVGL/GUI/lvgl/src/misc
				 LVGL/GUI/lvgl/src/widgets


				 LVGL/GUI_APP/demos
				 LVGL/GUI_APP/demos/stress
				 LVGL/GUI_APP/demos/widgets
				 LVGL/GUI_APP/demos/widgets/assets
				 LVGL/GUI_APP/demos/music
				 LVGL/GUI_APP/demos/music/assets
				 LVGL/GUI_APP/demos/keypad_encoder
				 LVGL/GUI_APP/demos/benchmark


				 LVGL/GUI_APP/user_home
				 LVGL/GUI_APP/app_shell


)

set(requires driver
			 esp_lcd
			 esp_driver_uart
			 esp_wifi
             esp_event
			 esp_http_server
			 cjson
			 spiffs
			 mqtt
			 app_update
             BSP
)

idf_component_register(SRCS ${src_dirs}
						   INCLUDE_DIRS ${include_dirs}
						   REQUIRES ${requires}
)


component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format -Wno-error=cast-function-type)