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
 * Translators: DG3GSP, DL4LEX, HB9WCN
 *
 *
 * Rev: 5
*/
#ifndef USER_INTERFACE_LANGUAGES_GERMAN_H_
#define USER_INTERFACE_LANGUAGES_GERMAN_H_
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
const stringsTable_t germanLanguage =
{
.magicNumber                            = { LANGUAGE_TAG_MAGIC_NUMBER, LANGUAGE_TAG_VERSION },
.LANGUAGE_NAME 				= "Deutsch", // MaxLen: 16
.menu					= "Menü", // MaxLen: 16
.credits				= "Mitwirkende", // MaxLen: 16
.zone					= "Zone", // MaxLen: 16
.rssi					= "Feldstärke", // MaxLen: 16
.battery				= "Akku", // MaxLen: 16
.contacts				= "Kontakte", // MaxLen: 16
.last_heard				= "Zuletzt gehört", // MaxLen: 16
.firmware_info				= "Firmware Info", // MaxLen: 16
.options				= "Einstellungen", // MaxLen: 16
.display_options			= "Display Optionen", // MaxLen: 16
.sound_options				= "Audio Optionen", // MaxLen: 16
.channel_details			= "Kanal Details", // MaxLen: 16
.language				= "Sprache", // MaxLen: 16
.new_contact				= "Neuer Kontakt", // MaxLen: 16
.dmr_contacts				= "DMR Kontakte", // MaxLen: 16
.contact_details			= "Kontakt Details", // MaxLen: 16
.hotspot_mode				= "Hotspot", // MaxLen: 16
.built					= "Erstellt", // MaxLen: 16
.zones					= "Zonen", // MaxLen: 16
.keypad					= "Tasten", // MaxLen: 12 (with .ptt)
.ptt					= "PTT", // MaxLen: 12 (with .keypad)
.locked					= "Gesperrt", // MaxLen: 15
.press_sk2_plus_star			= "SK2 Taste + *", // MaxLen: 16
.to_unlock				= "zum entsperren", // MaxLen: 16
.unlocked				= "Entsperrt", // MaxLen: 15
.power_off				= "Schalte aus...", // MaxLen: 16
.error					= "FEHLER", // MaxLen: 8
.rx_only				= "Nur RX", // MaxLen: 16
.out_of_band				= "AUSSER BAND", // MaxLen: 16
.timeout				= "Timeout", // MaxLen: 8
.tg_entry				= "TG Eingabe", // MaxLen: 15
.pc_entry				= "PC Eingabe", // MaxLen: 15
.user_dmr_id				= "Benutzer ID", // MaxLen: 15
.contact 				= "Kontakt", // MaxLen: 15
.accept_call				= "PC erlauben", // MaxLen: 16
.private_call				= "Privater Ruf", // MaxLen: 16
.squelch				= "Squelch",  // MaxLen: 8
.quick_menu 				= "Schnellfunktion", // MaxLen: 16
.filter					= "Filter", // MaxLen: 7 (with ':' + settings: "None", "CC", "CC,TS", "CC,TS,TG")
.all_channels				= "Alle Kanäle", // MaxLen: 16
.gotoChannel				= "Gehe zu",  // MaxLen: 11 (" 1024")
.scan					= "Suchlauf", // MaxLen: 16
.channelToVfo				= "Kanal -> VFO", // MaxLen: 16
.vfoToChannel				= "VFO -> Kanal", // MaxLen: 16
.vfoToNewChannel			= "VFO -> Neu Kanal", // MaxLen: 16
.group					= "Gruppe", // MaxLen: 16 (with .type)
.private				= "Privat", // MaxLen: 16 (with .type)
.all					= "Alle", // MaxLen: 16 (with .type)
.type					= "Type", // MaxLen: 16 (with .type)
.timeSlot				= "Zeitschlitz", // MaxLen: 16 (plus ':' and  .none, '1' or '2')
.none					= "Kein", // MaxLen: 16 (with .timeSlot, "RX CTCSS:" and ""TX CTCSS:")
.contact_saved				= "Kontakt gesp.", // MaxLen: 16
.duplicate				= "Duplikat", // MaxLen: 16
.tg					= "TG", // MaxLen: 8
.pc					= "PC", // MaxLen: 8
.ts					= "TS", // MaxLen: 8
.mode					= "Modus",  // MaxLen: 12
.colour_code				= "Color Code", // MaxLen: 16 (with ':' * .n_a)
.n_a					= "N/A", // MaxLen: 16 (with ':' * .colour_code)
.bandwidth				= "BW", // MaxLen: 16 (with : + .n_a, "25kHz" or "12.5kHz")
.stepFreq				= "Schritt", // MaxLen: 7 (with ':' + xx.xxkHz fitted)
.tot					= "TOT", // MaxLen: 16 (with ':' + .off or 15..3825)
.off					= "Aus", // MaxLen: 16 (with ':' + .timeout_beep, .band_limits)
.zone_skip				= "Skip Zone", // MaxLen: 16 (with ':' + .yes or .no)
.all_skip				= "Skip Alle", // MaxLen: 16 (with ':' + .yes or .no)
.yes					= "Ja", // MaxLen: 16 (with ':' + .zone_skip, .all_skip)
.no					= "Nein", // MaxLen: 16 (with ':' + .zone_skip, .all_skip)
.tg_list				= "TG-Liste", // MaxLen: 16 (with ':' and codeplug group name)
.on					= "Ein", // MaxLen: 16 (with ':' + .band_limits)
.timeout_beep				= "Timeout-Beep", // MaxLen: 16 (with ':' + .n_a or 5..20 + 's')
.list_full				= "Liste voll",
.dmr_cc_scan				= "CC Scan", // MaxLen: 12 (with ':' + settings: .on or .off)
.band_limits				= "Band Limit", // MaxLen: 16 (with ':' + .on or .off)
.beep_volume				= "Beep Lauts", // MaxLen: 16 (with ':' + -24..6 + 'dB')
.dmr_mic_gain				= "DMR Mikro", // MaxLen: 16 (with ':' + -33..12 + 'dB')
.fm_mic_gain				= "FM Mikro", // MaxLen: 16 (with ':' + 0..31)
.key_long				= "Key lang", // MaxLen: 11 (with ':' + x.xs fitted)
.key_repeat				= "Key wied", // MaxLen: 11 (with ':' + x.xs fitted)
.dmr_filter_timeout			= "DMR Filter", // MaxLen: 16 (with ':' + 1..90 + 's')
.brightness				= "Helligkeit", // MaxLen: 16 (with ':' + 0..100 + '%')
.brightness_off				= "Min Helligk.", // MaxLen: 16 (with ':' + 0..100 + '%')
.contrast				= "Kontrast", // MaxLen: 16 (with ':' + 12..30)
.screen_invert				= "Invers", // MaxLen: 16
.screen_normal				= "Normal", // MaxLen: 16
.backlight_timeout			= "Timeout", // MaxLen: 16 (with ':' + .no to 30s)
.scan_delay				= "Scan Verzög", // MaxLen: 16 (with ':' + 1..30 + 's')
.yes___in_uppercase			= "JA", // MaxLen: 8 (choice above green/red buttons)
.no___in_uppercase			= "NEIN", // MaxLen: 8 (choice above green/red buttons)
.DISMISS				= "ABLEHNEN", // MaxLen: 8 (choice above green/red buttons)
.scan_mode				= "Scan Modus", // MaxLen: 16 (with ':' + .hold or .pause)
.hold					= "Halt", // MaxLen: 16 (with ':' + .scan_mode)
.pause					= "Pause", // MaxLen: 16 (with ':' + .scan_mode)
.list_empty				= "Leere Liste", // MaxLen: 16
.delete_contact_qm			= "Kontakt löschen?", // MaxLen: 16
.contact_deleted			= "Kontakt gelöscht", // MaxLen: 16
.contact_used				= "Kontakt benutzt", // MaxLen: 16
.in_tg_list				= "in der TG-Liste", // MaxLen: 16
.select_tx				= "Wähle TX", // MaxLen: 16
.edit_contact				= "Kontakt ändern", // MaxLen: 16
.delete_contact				= "Kontakt löschen", // MaxLen: 16
.group_call				= "Gruppenruf", // MaxLen: 16
.all_call				= "Ruf an alle", // MaxLen: 16
.tone_scan				= "CTCSS Scan",//// MaxLen: 16
.low_battery				= "AKKU LEER!",//// MaxLen: 16
.Auto					= "Auto", // MaxLen 16 (with .mode + ':')
.manual					= "Manuell",  // MaxLen 16 (with .mode + ':')
.ptt_toggle				= "PTT bistabil", // MaxLen 16 (with ':' + .on or .off)
.private_call_handling			= "Anruf Hinw.", // MaxLen 16 (with ':' + .on ot .off)
.stop					= "Stop", // Maxlen 16 (with ':' + .scan_mode)
.one_line				= "1 Zeile", // MaxLen 16 (with ':' + .contact)
.two_lines				= "2 Zeilen", // MaxLen 16 (with ':' + .contact)
.new_channel				= "Neuer Kanal", // MaxLen: 16, leave room for a space and four channel digits after
.priority_order				= "ID-Prio", // MaxLen 16 (with ':' + 'Cc/DB/TA')
.dmr_beep				= "DMR Beep", // MaxLen 16 (with ':' + .star/.stop/.both/.none)
.start					= "Start", // MaxLen 16 (with ':' + .dmr_beep)
.both					= "Beide", // MaxLen 16 (with ':' + .dmr_beep)
.vox_threshold				= "VOX Empf.", // MaxLen 16 (with ':' + .off or 1..30)
.vox_tail				= "VOX Dauer", // MaxLen 16 (with ':' + .n_a or '0.0s')
.audio_prompt				= "Töne",// Maxlen 16 (with ':' + .silent, .beep or .voice_prompt_level_1)
.silent					= "Aus", // Maxlen 16 (with : + audio_prompt)
.rx_beep				= "RX Beep", // MaxLen 16 (with ':' + .carrier/.talker/.both/.none)
.beep					= "Ein", // Maxlen 16 (with : + audio_prompt)
.voice_prompt_level_1			= "Stimme L1", // Maxlen 16 (with : + audio_prompt, satellite "mode")
.transmitTalkerAliasTS1			= "TA TX TS1", // Maxlen 16 (with : + .on or .off)
.squelch_VHF				= "VHF Squelch",// Maxlen 16 (with : + XX%)
.squelch_220				= "220 Squelch",// Maxlen 16 (with : + XX%)
.squelch_UHF				= "UHF Squelch", // Maxlen 16 (with : + XX%)
.display_screen_invert			= "Anzeige" , // Maxlen 16 (with : + .screen_normal or .screen_invert)
.openGD77 				= "OpenGD77",// Do not translate
.talkaround 				= "Talkaround", // Maxlen 16 (with ':' + .on , .off or .n_a)
.APRS 					= "APRS", // Maxlen 16 (with : + .transmitTalkerAliasTS1 or transmitTalkerAliasTS2)
.no_keys 				= "Tasten aus", // Maxlen 16 (with : + audio_prompt)
.gitCommit				= "Git Übergabe",
.voice_prompt_level_2			= "Stimme L2", // Maxlen 16 (with : + audio_prompt)
.voice_prompt_level_3			= "Stimme L3", // Maxlen 16 (with : + audio_prompt)
.dmr_filter				= "DMR Filter",// MaxLen: 12 (with ':' + settings: "TG" or "Ct" or "TGL")
.talker					= "Talker",
.dmr_ts_filter				= "TS Filter", // MaxLen: 12 (with ':' + settings: .on or .off)
.dtmf_contact_list			= "FM DTMF Kontakte", // Maxlen: 16
.channel_power				= "Kal Leist.", //Displayed as "Ch Power:" + .from_master or "Ch Power:"+ power text e.g. "Power:500mW" . Max total length 16
.from_master				= "Master",// Displayed if per-channel power is not enabled  the .channel_power
.set_quickkey				= "Quickkey belegen", // MaxLen: 16
.dual_watch				= "Dual Watch", // MaxLen: 16
.info					= "Infoleiste", // MaxLen: 16 (with ':' + .off or.ts or .pwr or .both)
.pwr					= "Leistung",
.user_power				= "User Power",
.temperature				= "Temperatur", // MaxLen: 16 (with ':' + .celcius or .fahrenheit)
.celcius				= "°C",
.seconds				= "Sekunden",
.radio_info				= "Radio Infos",
.temperature_calibration		= "Temp.Kal", // MaxLen: 8
.pin_code				= "Pin Code",
.please_confirm				= "Bitte bestätigen", // MaxLen: 15
.vfo_freq_bind_mode			= "Freq. Bind",
.overwrite_qm				= "Überschreiben?", //Maxlen: 14 chars
.eco_level				= "ECO Stufe",
.buttons				= "Tasten",
.leds					= "LEDs",
.scan_dwell_time			= "Scan Halt",
.battery_calibration			= "Batt. Kal", // MaxLen: 9
.low					= "Nieder",
.high					= "Hoch",
.dmr_id					= "DMR ID",
.scan_on_boot				= "Scan @ Start", //Maxlen: 12 chars
.dtmf_entry				= "DTMF Eintrag",
.name					= "Name",
.carrier				= "Träger",
.zone_empty 				= "Zone leer", // Maxlen: 12 chars.
.time					= "Zeit",
.uptime					= "Uptime",
.hours					= "Stunden",
.minutes				= "Minuten",
.satellite				= "Satellit",
.alarm_time				= "Alarmzeit",
.location				= "Standort",
.date					= "Datum",
.timeZone				= "Zeitzone",
.suspend				= "Standby",
.pass					= "Pass", // For satellite screen
.elevation				= "El",
.azimuth				= "Az",
.inHHMMSS				= "in",
.predicting				= "Prognose",
.maximum				= "Max",
.satellite_short			= "Sat",
.local					= "Lokal",
.UTC					= "UTC",
.symbols				= "NSOW", // symbols: N,S,E,W
.not_set				= "NICHT GESETZT",
.general_options			= "Einstellungen",
.radio_options				= "Radio Optionen",
.auto_night				= "Auto Nacht", // MaxLen: 16 (with .on or .off)
.dmr_rx_agc				= "DMR RX AGC",
.speaker_click_suppress			= "Klick Unterd.",
.gps					= "GPS",
.end_only				= "am Ende",
.dmr_crc				= "DMR CRC",
.eco					= "ECO",
.safe_power_on				= "ges. Ansch.", // MaxLen: 16 (with ':' + .on or .off)
.auto_power_off				= "Auto PWR-Aus", // MaxLen: 16 (with ':' + 30/60/90/120/180 or .no)
.apo_with_rf				= "APO mit RF", // MaxLen: 16 (with ':' + .yes or .no or .n_a)
.brightness_night			= "Nacht Hell.", // MaxLen: 16 (with : + 0..100 + %)
.freq_set_VHF				= "Freq VHF",
.gps_acquiring				= "GPS Übern.",
.altitude				= "Höhe",
.calibration				= "Kalibration",
.freq_set_UHF				= "Freq UHF",
.cal_frequency				= "Kal Freq",
.cal_pwr				= "Kal Power",
.pwr_set				= "Power Einst",
.factory_reset				= "Factory Kal",
.rx_tune				= "RX Tuning",
.transmitTalkerAliasTS2			= "TA TX TS2", // Maxlen 16 (with : + .ta_text, 'APRS' , .both or .off)
.ta_text				= "Text",
.daytime_theme_day			= "Tag Theme", // MaxLen: 16
.daytime_theme_night			= "Nacht Theme", // MaxLen: 16
.theme_chooser				= "Theme wählen", // Maxlen: 16
.theme_options				= "Theme Optionen",
.theme_fg_default			= "Standardtext", // MaxLen: 16 (+ colour rect)
.theme_bg				= "Hintergrund", // MaxLen: 16 (+ colour rect)
.theme_fg_decoration			= "Umrandung", // MaxLen: 16 (+ colour rect)
.theme_fg_text_input			= "Texteingabe", // MaxLen: 16 (+ colour rect)
.theme_fg_splashscreen			= "Startbildschirm", // MaxLen: 16 (+ colour rect)
.theme_bg_splashscreen			= "Startbild. Hint.", // MaxLen: 16 (+ colour rect)
.theme_fg_notification			= "Textmeldung", // MaxLen: 16 (+ colour rect)
.theme_fg_warning_notification		= "Warnmeldung", // MaxLen: 16 (+ colour rect)
.theme_fg_error_notification		= "Fehlermeldung", // MaxLen: 16 (+ colour rect)
.theme_bg_notification			= "Meldung Hint.", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_name			= "Menü Name", // MaxLen: 16 (+ colour rect)
.theme_bg_menu_name			= "Menü Name Hint.", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_item			= "Menüpunkt", // MaxLen: 16 (+ colour rect)
.theme_fg_menu_item_selected		= "Menü ausgewählt", // MaxLen: 16 (+ colour rect)
.theme_fg_options_value			= "Option Wert", // MaxLen: 16 (+ colour rect)
.theme_fg_header_text			= "Überschrift", // MaxLen: 16 (+ colour rect)
.theme_bg_header_text			= "Überschrift bkg", // MaxLen: 16 (+ colour rect)
.theme_fg_rssi_bar			= "RSSI Balken", // MaxLen: 16 (+ colour rect)
.theme_fg_rssi_bar_s9p			= "RSSI Balk.S9+", // Maxlen: 16 (+colour rect)
.theme_fg_channel_name			= "Kanal Name", // MaxLen: 16 (+ colour rect)
.theme_fg_channel_contact		= "Kontakt", // MaxLen: 16 (+ colour rect)
.theme_fg_channel_contact_info		= "Kontakt Info", // MaxLen: 16 (+ colour rect)
.theme_fg_zone_name			= "Zonen Name", // MaxLen: 16 (+ colour rect)
.theme_fg_rx_freq			= "RX Freq", // MaxLen: 16 (+ colour rect)
.theme_fg_tx_freq			= "TX Freq", // MaxLen: 16 (+ colour rect)
.theme_fg_css_sql_values		= "CSS/SQL Werte", // MaxLen: 16 (+ colour rect)
.theme_fg_tx_counter			= "TX Zähler", // MaxLen: 16 (+ colour rect)
.theme_fg_polar_drawing			= "Polar", // MaxLen: 16 (+ colour rect)
.theme_fg_satellite_colour		= "Sat. Punkt", // MaxLen: 16 (+ colour rect)
.theme_fg_gps_number			= "GPS Nummer", // MaxLen: 16 (+ colour rect)
.theme_fg_gps_colour			= "GPS Punkt", // MaxLen: 16 (+ colour rect)
.theme_fg_bd_colour			= "BeiDou Punkt", // MaxLen: 16 (+ colour rect)
.theme_colour_picker_red		= "Rot", // MaxLen 16 (with ':' + 3 digits value)
.theme_colour_picker_green		= "Grün", // MaxLen 16 (with ':' + 3 digits value)
.theme_colour_picker_blue		= "Blau", // MaxLen 16 (with ':' + 3 digits value)
.volume					= "Lautst.", // MaxLen: 8
.distance_sort				= "Dist. Sort", // MaxLen 16 (with ':' + .on or .off)
.show_distance				= "Zeige Dist.", // MaxLen 16 (with ':' + .on or .off)
.aprs_options				= "APRS Optionen", // MaxLen 16
.aprs_smart				= "Smart", // MaxLen 16 (with ':' + .mode)
.aprs_channel				= "Kanal", // MaxLen 16 (with ':' + .location)
.aprs_decay				= "Verfall", // MaxLen 16 (with ':' + .on or .off)
.aprs_compress				= "Komprim.", // MaxLen 16 (with ':' + .on or .off)
.aprs_interval				= "Interval", // MaxLen 16 (with ':' + 0.2..60 + 'min')
.aprs_message_interval			= "Msg Interval", // MaxLen 16 (with ':' + 3..30)
.aprs_slow_rate				= "Slow Rate", // MaxLen 16 (with ':' + 1..100 + 'min')
.aprs_fast_rate				= "Fast Rate", // MaxLen 16 (with ':' + 10..180 + 's')
.aprs_low_speed				= "Low Speed", // MaxLen 16 (with ':' + 2..30 + 'km/h')
.aprs_high_speed			= "Hi Speed", // MaxLen 16 (with ':' + 2..90 + 'km/h')
.aprs_turn_angle			= "T. Angle", // MaxLen 16 (with ':' + 5..90 + '°')
.aprs_turn_slope			= "T. Slope", // MaxLen 16 (with ':' + 1..255 + '°/v')
.aprs_turn_time				= "T. Time", // MaxLen 16 (with ':' + 5..180 + 's')
.auto_lock				= "Auto Lock", // MaxLen 16 (with ':' + .off or 0.5..15 (.5 step) + 'min')
.trackball				= "Trackball", // MaxLen 16 (with ':' + .on or .off)
.dmr_force_dmo				= "DMO Zwang", // MaxLen 16 (with ':' + .n_a or .on or .off)
.games					= "Spiele", // MaxLen 16
.aprsLog				= "Logs", // MaxLen 16 - APRS, call and Morse logs
.userManual				= "Handbuch", // MaxLen 16
.channelZone			= "Funk-Setup", // MaxLen 16
.fiveToneContacts		= "5T Kontakte"
};
/********************************************************************
 *
 * VERY IMPORTANT.
 * This file should not be saved with UTF-8 encoding
 * Use Notepad++ on Windows with ANSI encoding
 * or emacs on Linux with windows-1252-unix encoding
 *
 ********************************************************************/
#endif /* USER_INTERFACE_LANGUAGES_GERMAN_H  */
