/* -*- coding: windows-1252-unix; -*- */
/*
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
 /*
 * Translators: EA3IGM, EA3BIL, EA5SW
 *
 *
 * Rev: 05Marzo2024 EA3BIL
 */
#ifndef USER_INTERFACE_LANGUAGES_SPANISH_H_
#define USER_INTERFACE_LANGUAGES_SPANISH_H_
/********************************************************************
 *
 * VERY IMPORTANT.
 * This file should not be saved with UTF-8 encoding
 * Use Notepad++ on Windows with ANSI encoding
 * or emacs on Linux with windows-1252-unix encoding
 *
 ********************************************************************/
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
__attribute__((section(".upper_text")))
#endif
const stringsTable_t spanishLanguage =
{
.magicNumber                            = { LANGUAGE_TAG_MAGIC_NUMBER, LANGUAGE_TAG_VERSION },
.LANGUAGE_NAME 			= "Español", // MaxLen: 16 - shown in its own language, like "Türkçe"
.menu					= "Menú", // MaxLen: 16
.credits				= "Créditos", // MaxLen: 16
.zone					= "Zona", // MaxLen: 16
.rssi					= "RSSI", // MaxLen: 16
.battery				= "Batería", // MaxLen: 16
.contacts				= "Contactos", // MaxLen: 16
.last_heard				= "Escuchados", // MaxLen: 16
.firmware_info			= "Info Firmware", // MaxLen: 16
.options				= "Opciones", // MaxLen: 16
.display_options		= "Opciones Display", // MaxLen: 16
.sound_options			= "Opciones Sonido", // MaxLen: 16
.channel_details		= "Detalles Canal", // MaxLen: 16
.language				= "Idioma", // MaxLen: 16
.new_contact			= "Nuevo contacto", // MaxLen: 16
.dmr_contacts			= "DMR contactos", // MaxLen: 16
.contact_details		= "Detalles Ctto.", // MaxLen: 16
.hotspot_mode			= "Hotspot", // MaxLen: 16
.built					= "Compilado", // MaxLen: 16
.zones					= "Zonas", // MaxLen: 16
.keypad					= "Teclado", // MaxLen: 12 (with .ptt)
.ptt					= "PTT", // MaxLen: 12 (with .keypad)
.locked					= "Bloqueado", // MaxLen: 15
.press_sk2_plus_star	= "Pulsa SK2 + *", // MaxLen: 16
.to_unlock				= "para desbloquear", // MaxLen: 16
.unlocked				= "Desbloqueado", // MaxLen: 15
.power_off				= "Apagando...", // MaxLen: 16
.error					= "ERROR", // MaxLen: 8
.rx_only				= "Solo Rx", // MaxLen: 14
.out_of_band			= "FUERA DE BANDA", // MaxLen: 14
.timeout				= "TIMEOUT", // MaxLen: 8
.tg_entry				= "Entrar TG", // MaxLen: 15
.pc_entry				= "Entrar PC", // MaxLen: 15
.user_dmr_id			= "ID DMR Usuario", // MaxLen: 15
.contact 				= "Contacto", // MaxLen: 15
.accept_call			= "Aceptar PC?", // MaxLen: 16
.private_call			= "Llamada Privada", // MaxLen: 16
.squelch				= "Squelch", // MaxLen: 8
.quick_menu 			= "Menú rápido", // MaxLen: 16
.filter					= "Filtro", // MaxLen: 7 (with ':' + settings: "None", "CC", "CC,TS", "CC,TS,TG")
.all_channels			= "Lista Canales", // MaxLen: 16
.gotoChannel			= "Ir canal",  // MaxLen: 11 (" 1024")
.scan					= "Escanear", // MaxLen: 16
.channelToVfo			= "Canal --> VFO", // MaxLen: 16
.vfoToChannel			= "VFO --> Canal", // MaxLen: 16
.vfoToNewChannel		= "VFO --> Nuevo C.", // MaxLen: 16
.group					= "Grupo", // MaxLen: 16 (with .type)
.private				= "Privado", // MaxLen: 16 (with .type)
.all					= "Todos", // MaxLen: 16 (with .type)
.type					= "Tipo", // MaxLen: 16 (with .type)
.timeSlot				= "Timeslot", // MaxLen: 16 (plus ':' and  .none, '1' or '2')
.none					= "Ninguno", // MaxLen: 16 (with .timeSlot, "Rx CTCSS:" and ""Tx CTCSS:")
.contact_saved			= "Contacto grabado", // MaxLen: 16
.duplicate				= "Duplicado", // MaxLen: 16
.tg						= "TG",  // MaxLen: 8
.pc						= "PC", // MaxLen: 8
.ts						= "TS", // MaxLen: 8
.mode					= "Modo",  // MaxLen: 12
.colour_code			= "Codigo Color", // MaxLen: 16 (with ':' * .n_a)
.n_a					= "N/A",// MaxLen: 16 (with ':' * .colour_code)
.bandwidth				= "BW", // MaxLen: 16 (with : + .n_a, "25kHz" or "12.5kHz")
.stepFreq				= "Paso", // MaxLen: 7 (with ':' + xx.xxkHz fitted)
.tot					= "TOT", // MaxLen: 16 (with ':' + .off or 15..3825)
.off					= "Off", // MaxLen: 16 (with ':' + .timeout_beep, .band_limits)
.zone_skip				= "Saltar zona", // MaxLen: 16 (with ':' + .yes or .no)
.all_skip				= "Saltar todo", // MaxLen: 16 (with ':' + .yes or .no)
.yes					= "Sí", // MaxLen: 16 (with ':' + .zone_skip, .all_skip)
.no						= "No", // MaxLen: 16 (with ':' + .zone_skip, .all_skip)
.tg_list				= "Lst TG", // MaxLen: 16 (with ':' and codeplug group name)
.on						= "On", // MaxLen: 16 (with ':' + .band_limits)
.timeout_beep			= "Sonido TOT", // MaxLen: 16 (with ':' + .n_a or 5..20 + 's')
.list_full				= "Lista Llena",
.dmr_cc_scan			= "CC Scan", // MaxLen: 12 (with ':' + settings: .on or .off)
.band_limits			= "Límite Banda", // MaxLen: 16 (with ':' + .on or .off)
.beep_volume			= "Vol. BEEP", // MaxLen: 16 (with ':' + -24..6 + 'dB')
.dmr_mic_gain			= "Micro DMR", // MaxLen: 16 (with ':' + -33..12 + 'dB')
.fm_mic_gain			= "Micro FM", // MaxLen: 16 (with ':' + 0..31)
.key_long				= "Tec.larga", // MaxLen: 11 (with ':' + x.xs fitted)
.key_repeat				= "Tec.repetir", // MaxLen: 11 (with ':' + x.xs fitted)
.dmr_filter_timeout		= "Fil. tiempo", // MaxLen: 16 (with ':' + 1..90 + 's')
.brightness				= "Brillo", // MaxLen: 16 (with ':' + 0..100 + '%')
.brightness_off			= "Brillo min", // MaxLen: 16 (with ':' + 0..100 + '%')
.contrast				= "Contraste", // MaxLen: 16 (with ':' + 12..30)
.screen_invert			= "Invertido", // MaxLen: 16
.screen_normal			= "Normal", // MaxLen: 16
.backlight_timeout		= "Tiempo luz", // MaxLen: 16 (with ':' + .no to 30s)
.scan_delay				= "Tiempo Scan", // MaxLen: 16 (with ':' + 1..30 + 's')
.yes___in_uppercase		= "SÍ", // MaxLen: 8 (choice above green/red buttons)
.no___in_uppercase		= "NO", // MaxLen: 8 (choice above green/red buttons)
.DISMISS				= "OMITIR", // MaxLen: 8 (choice above green/red buttons)
.scan_mode				= "Modo Scan", // MaxLen: 16 (with ':' + .hold or .pause)
.hold					= "Deten", // MaxLen: 16 (with ':' + .scan_mode)
.pause					= "Pausa", // MaxLen: 16 (with ':' + .scan_mode)
.list_empty				= "Lista vacía", // MaxLen: 16
.delete_contact_qm		= "Borrar contacto?", // MaxLen: 16
.contact_deleted		= "Contacto borrado", // MaxLen: 16
.contact_used			= "Contacto usado", // MaxLen: 16
.in_tg_list			= "en la lista TG", // MaxLen: 16
.select_tx				= "Selecciona TX", // MaxLen: 16
.edit_contact			= "Editar contacto", // MaxLen: 16
.delete_contact			= "Borrar contacto", // MaxLen: 16
.group_call				= "Llamada Grupo", // MaxLen: 16
.all_call				= "Llamada Todos", // MaxLen: 16
.tone_scan				= "Tono scan",//// MaxLen: 16
.low_battery			= "BATERÍA BAJA!!!",//// MaxLen: 16
.Auto					= "Auto", // MaxLen 16 (with .mode + ':')
.manual					= "Manual",  // MaxLen 16 (with .mode + ':')
.ptt_toggle				= "PTT Fijo", // MaxLen 16 (with ':' + .on or .off)
.private_call_handling	= "Filtro PC", // MaxLen 16 (with ':' + .on ot .off)
.stop					= "Parar", // Maxlen 16 (with ':' + .scan_mode)
.one_line				= "1 linea", // MaxLen 16 (with ':' + .contact)
.two_lines				= "2 linea", // MaxLen 16 (with ':' + .contact)
.new_channel			= "Nuevo canal", // MaxLen: 16, leave room for a space and four channel digits after
.priority_order			= "Orden", // MaxLen 16 (with ':' + 'Cc/DB/TA')
.dmr_beep				= "DMR beep", // MaxLen 16 (with ':' + .star/.stop/.both/.none)
.start					= "Inicio", // MaxLen 16 (with ':' + .dmr_beep)
.both					= "Ambos", // MaxLen 16 (with ':' + .dmr_beep)
.vox_threshold			= "VOX Gan.", // MaxLen 16 (with ':' + .off or 1..30)
.vox_tail				= "VOX Cola", // MaxLen 16 (with ':' + .n_a or '0.0s')
.audio_prompt			= "Prompt",// Maxlen 16 (with ':' + .silent, .beep or .voice_prompt_level_1)
.silent                 = "Silencio", // Maxlen 16 (with : + audio_prompt)
.rx_beep				= "RX beep", // MaxLen 16 (with ':' + .carrier/.talker/.both/.none)
.beep					= "Beep", // Maxlen 16 (with : + audio_prompt)
.voice_prompt_level_1	= "Voz", // Maxlen 16 (with : + audio_prompt, satellite "mode")
.transmitTalkerAliasTS1	= "TA Tx TS1", // Maxlen 16 (with : + .on or .off)
.squelch_VHF			= "VHF Squelch",// Maxlen 16 (with : + XX%)
.squelch_220			= "220 Squelch",// Maxlen 16 (with : + XX%)
.squelch_UHF			= "UHF Squelch", // Maxlen 16 (with : + XX%)
.display_screen_invert = "Color" , // Maxlen 16 (with : + .screen_normal or .screen_invert)
.openGD77 				= "OpenGD77",// Do not translate
.talkaround 				= "Talkaround", // Maxlen 16 (with ':' + .on , .off or .n_a)
.APRS 					= "APRS", // Maxlen 16 (with : + .transmitTalkerAliasTS1 or transmitTalkerAliasTS2)
.no_keys 				= "No Keys", // Maxlen 16 (with : + audio_prompt)
.gitCommit				= "Git commit",
.voice_prompt_level_2	= "Voz L2", // Maxlen 16 (with : + audio_prompt)
.voice_prompt_level_3	= "Voz L3", // Maxlen 16 (with : + audio_prompt)
.dmr_filter				= "Filtro DMR",// MaxLen: 12 (with ':' + settings: "TG" or "Ct" or "TGL")
.talker					= "Talker",
.dmr_ts_filter			= "Filtro TS", // MaxLen: 12 (with ':' + settings: .on or .off)
.dtmf_contact_list		= "FM DTMF contact.", // Maxlen: 16
.channel_power			= "Pot. Canal", //Displayed as "Ch Power:" + .from_master or "Ch Power:"+ power text e.g. "Power:500mW" . Max total length 16
.from_master			= "Master",// Displayed if per-channel power is not enabled  the .channel_power
.set_quickkey			= "Poner Quickkey", // MaxLen: 16
.dual_watch				= "Dual Watch", // MaxLen: 16
.info					= "Info", // MaxLen: 16 (with ':' + .off or.ts or .pwr or .both)
.pwr					= "Pwr",
.user_power				= "Pot.Usuario",
.temperature			= "Temp.", // MaxLen: 16 (with ':' + .celcius or .fahrenheit)
.celcius				= "°C",
.seconds				= "segundos",
.radio_info				= "Radio info",
.temperature_calibration	= "Cal. Temp.",
.pin_code				= "Código Pin",
.please_confirm			= "Confirmar", // MaxLen: 15
.vfo_freq_bind_mode		= "Copiar Frec.",
.overwrite_qm			= "Sobrescribir?", //Maxlen: 14 chars
.eco_level				= "Nivel Eco",
.buttons				= "Botones",
.leds					= "LEDs",
.scan_dwell_time		= "Muestreo",
.battery_calibration	= "Cal.Batería",
.low					= "Baja",
.high					= "Alta",
.dmr_id					= "DMR ID",
.scan_on_boot			= "Escan inicio",
.dtmf_entry				= "Entrar DTMF",
.name					= "Nombre",
.carrier				= "Carrier",
.zone_empty 				= "Zona vacía", // Maxlen: 12 chars.
.time					= "Hora",
.uptime					= "Hora Encendido",
.hours					= "Horas",
.minutes				= "Minutos",
.satellite				= "Satélite",
.alarm_time				= "Hora Alarma",
.location				= "Ubicación",
.date					= "Fecha",
.timeZone				= "Zona Horaria",
.suspend				= "Hibernar",
.pass					= "Pase", // For satellite screen
.elevation				= "El",
.azimuth				= "Az",
.inHHMMSS				= "en",
.predicting				= "Predicción",
.maximum				= "Máx",
.satellite_short		= "Sat",
.local					= "Local",
.UTC					= "UTC",
.symbols				= "NSEO", // symbols: N,S,E,W
.not_set				= "NO DEFINIDO",
.general_options		= "Opciones Grales.",
.radio_options			= "Opciones Radio",
.auto_night				= "Auto noche", // MaxLen: 16 (with .on or .off)
.dmr_rx_agc				= "DMR Rx CAG",
.speaker_click_suppress			= "Quitar Click",
.gps					= "GPS",
.end_only				= "Sólo Fin",
.dmr_crc				= "CRC DMR",
.eco					= "Eco",
.safe_power_on				= "Safe Pwr-On", // MaxLen: 16 (with ':' + .on or .off)
.auto_power_off				= "Auto Pwr-Off", // MaxLen: 16 (with ':' + 30/60/90/120/180 or .no)
.apo_with_rf				= "APO with RF" , // MaxLen: 16 (with ':' + .yes or .no or .n_a)
.brightness_night				= "Luz Noche", // MaxLen: 16 (with : + 0..100 + %)
.freq_set_VHF			= "Frec VHF",
.gps_acquiring			= "Buscando",
.altitude				= "Alt",
.calibration            = "Calibración",
.freq_set_UHF                = "Frec UHF",
.cal_frequency          = "Frec",
.cal_pwr                = "Nivel Pot.",
.pwr_set                = "Ajuest Pot",
.factory_reset          = "Reset Fábrica",
.rx_tune				= "RX Sintonía",
.transmitTalkerAliasTS2	= "TA Tx TS2", // Maxlen 16 (with : + .ta_text, 'APRS' , .both or .off)
.ta_text				= "Texto",
.daytime_theme_day			= "Tema Día", // MaxLen: 16
.daytime_theme_night			= "Tema Noche", // MaxLen: 16
.theme_chooser				= "Selección Tema", // Maxlen: 16
.theme_options				= "Opciones Color",
.theme_fg_default			= "Texto Pred.", // MaxLen: 16 (+ colour rect)
.theme_bg				= "Fondo", // MaxLen: 16 (+ colour rect)
.theme_fg_decoration			= "Decoración", // MaxLen: 16 (+ colour rect)
.theme_fg_text_input			= "Texto Ent.", // MaxLen: 16 (+ colour rect)
.theme_fg_splashscreen			= "Inicio Texto", // MaxLen: 16 (+ colour rect)
.theme_bg_splashscreen			= "Inicio Fondo", // MaxLen: 16 (+ colour rect)
.theme_fg_notification			= "Texto notif.", // MaxLen: 16 (+ colour rect)
.theme_fg_warning_notification		= "Aviso notif.", // MaxLen: 16 (+ colour rect)
.theme_fg_error_notification		= "Error notif", // MaxLen: 16 (+ colour rect)
.theme_bg_notification                  = "Fondo notif", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_name			= "Nombre Menú", // MaxLen: 16 (+ colour rect)
.theme_bg_menu_name			= "Nombre Menú fon", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_item			= "Menú sel", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_item_selected		= "Menú Realce", // MaxLen: 16 (+ colour rect)
.theme_fg_options_value			= "Opción valor", // MaxLen: 16 (+ colour rect)
.theme_fg_header_text			= "Título text", // MaxLen: 16 (+ colour rect)
.theme_bg_header_text			= "Título text fon", // MaxLen: 16 (+ colour rect)
.theme_fg_rssi_bar			= "RSSI lin", // MaxLen: 16 (+ colour rect)
.theme_fg_rssi_bar_s9p			= "RSSI bar S9+", // Maxlen: 16 (+colour rect)
.theme_fg_channel_name			= "Canal nombre", // MaxLen: 16 (+ colour rect)
.theme_fg_channel_contact		= "Contacto", // MaxLen: 16 (+ colour rect)
.theme_fg_channel_contact_info		= "Contacto info", // MaxLen: 16 (+ colour rect)
.theme_fg_zone_name			= "Zona nom.", // MaxLen: 16 (+ colour rect)
.theme_fg_rx_freq			= "RX frec", // MaxLen: 16 (+ colour rect)
.theme_fg_tx_freq			= "TX frec", // MaxLen: 16 (+ colour rect)
.theme_fg_css_sql_values		= "CSS/SQL valor", // MaxLen: 16 (+ colour rect)
.theme_fg_tx_counter			= "TX cuenta", // MaxLen: 16 (+ colour rect)
.theme_fg_polar_drawing			= "Polar", // MaxLen: 16 (+ colour rect)
.theme_fg_satellite_colour		= "Sat. spot", // MaxLen: 16 (+ colour rect)
.theme_fg_gps_number			= "GPS núm.", // MaxLen: 16 (+ colour rect)
.theme_fg_gps_colour			= "GPS spot", // MaxLen: 16 (+ colour rect)
.theme_fg_bd_colour			= "BeiDou spot", // MaxLen: 16 (+ colour rect)
.theme_colour_picker_red		= "Rojo", // MaxLen 16 (with ':' + 3 digits value)
.theme_colour_picker_green		= "Verde", // MaxLen 16 (with ':' + 3 digits value)
.theme_colour_picker_blue		= "Azul", // MaxLen 16 (with ':' + 3 digits value)
.volume					= "Volume", // MaxLen: 8
.distance_sort				= "Dist sort", // MaxLen 16 (with ':' + .on or .off)
.show_distance				= "Show dist", // MaxLen 16 (with ':' + .on or .off)
.aprs_options				= "APRS options", // MaxLen 16
.aprs_smart				= "Smart", // MaxLen 16 (with ':' + .mode)
.aprs_channel				= "Channel", // MaxLen 16 (with ':' + .location)
.aprs_decay				= "Decay", // MaxLen 16 (with ':' + .on or .off)
.aprs_compress				= "Compress", // MaxLen 16 (with ':' + .on or .off)
.aprs_interval				= "Interval", // MaxLen 16 (with ':' + 0.2..60 + 'min')
.aprs_message_interval			= "Msg Interval", // MaxLen 16 (with ':' + 3..30)
.aprs_slow_rate				= "Slow Rate", // MaxLen 16 (with ':' + 1..100 + 'min')
.aprs_fast_rate				= "Fast Rate", // MaxLen 16 (with ':' + 10..180 + 's')
.aprs_low_speed				= "Low Speed", // MaxLen 16 (with ':' + 2..30 + 'km/h')
.aprs_high_speed			= "Hi Speed", // MaxLen 16 (with ':' + 2..90 + 'km/h')
.aprs_turn_angle			= "T. Angle", // MaxLen 16 (with ':' + 5..90 + '°')
.aprs_turn_slope			= "T. Slope", // MaxLen 16 (with ':' + 1..255 + '°/v')
.aprs_turn_time				= "T. Time", // MaxLen 16 (with ':' + 5..180 + 's')
.auto_lock				= "Auto lock", // MaxLen 16 (with ':' + .off or 0.5..15 (.5 step) + 'min')
.trackball				= "Trackball", // MaxLen 16 (with ':' + .on or .off)
.dmr_force_dmo				= "Force DMO", // MaxLen 16 (with ':' + .n_a or .on or .off)
.games					= "Juegos", // MaxLen 16
.aprsLog				= "Registros", // MaxLen 16 - APRS, call and Morse logs
.userManual				= "Manual", // MaxLen 16 ("Manual de usuario" is 17)
.channelZone			= "Config. radio", // MaxLen 16
.fiveToneContacts		= "Contactos 5T", // MaxLen 16
};
/********************************************************************
 *
 * VERY IMPORTANT.
 * This file should not be saved with UTF-8 encoding
 * Use Notepad++ on Windows with ANSI encoding
 * or emacs on Linux with windows-1252-unix encoding
 *
 ********************************************************************/
#endif /* USER_INTERFACE_LANGUAGES_SPANISH_H_ */
