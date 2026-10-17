/*
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
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
#ifndef _OPENGD77_UILANGUAGE_H_
#define _OPENGD77_UILANGUAGE_H_

#define LANGUAGE_TEXTS_LENGTH 17

#if defined(LANGUAGE_BUILD_JAPANESE)
#define LANGUAGE_TAG_MAGIC_NUMBER { 'I', 'G', 'N', 'R' }, { 'L', 'A', 'N', 'G' } // Never change this tag.
#else
#define LANGUAGE_TAG_MAGIC_NUMBER { 'G', 'D', '7', '7' }, { 'L', 'A', 'N', 'G' } // Never change this tag.
#endif
#define LANGUAGE_TAG_VERSION      { 0x00, 0x00, 0x00, 0x03 } // Bump the version each time the language struct is changed

typedef struct
{
	uint8_t magicNumber[3][4]; // 0..1: Tag (GD77LANG or IGNRLANG), 2: version
/*
 * IMPORTANT
 *
 * The first set of strings are used for menu names
 *
 * DO NOT RE-ORGANISE THIS LIST as the items are accessed using pointer arithmetic
 * from menuSystem. c mainMenuItems[]
 *
 */
   const char LANGUAGE_NAME[LANGUAGE_TEXTS_LENGTH];// Menu number 0
   const char menu[LANGUAGE_TEXTS_LENGTH];// Menu number  1
   const char credits[LANGUAGE_TEXTS_LENGTH];// Menu number  2
   const char zone[LANGUAGE_TEXTS_LENGTH];// Menu number 3
   const char rssi[LANGUAGE_TEXTS_LENGTH];// Menu number  4
   const char battery[LANGUAGE_TEXTS_LENGTH];// Menu number  5
   const char contacts[LANGUAGE_TEXTS_LENGTH];// Menu number  6
   const char last_heard[LANGUAGE_TEXTS_LENGTH];// Menu number  7
   const char firmware_info[LANGUAGE_TEXTS_LENGTH];// Menu number  8
   const char options[LANGUAGE_TEXTS_LENGTH];// Menu number  9
   const char display_options[LANGUAGE_TEXTS_LENGTH];// Menu number  10
   const char sound_options[LANGUAGE_TEXTS_LENGTH];// Menu number  11
   const char channel_details[LANGUAGE_TEXTS_LENGTH];// Menu number  12
   const char language[LANGUAGE_TEXTS_LENGTH];// Menu number  13
   const char new_contact[LANGUAGE_TEXTS_LENGTH];// Menu number  14
   const char dmr_contacts[LANGUAGE_TEXTS_LENGTH];// Menu number  15
   const char contact_details[LANGUAGE_TEXTS_LENGTH];// Menu number 16
   const char hotspot_mode[LANGUAGE_TEXTS_LENGTH];// Menu number 17

  /*
   * DO NOT RE-ORGANISE THIS LIST as the items are accessed using pointer arithmetic
   * for the voice prompts
   */

   const char built[LANGUAGE_TEXTS_LENGTH];//
   const char zones[LANGUAGE_TEXTS_LENGTH];//
   const char keypad[LANGUAGE_TEXTS_LENGTH];//
   const char ptt[LANGUAGE_TEXTS_LENGTH];//
   const char locked[LANGUAGE_TEXTS_LENGTH];//
   const char press_sk2_plus_star[LANGUAGE_TEXTS_LENGTH];//
   const char to_unlock[LANGUAGE_TEXTS_LENGTH];//
   const char unlocked[LANGUAGE_TEXTS_LENGTH];//
   const char power_off[LANGUAGE_TEXTS_LENGTH]; //
   const char error[LANGUAGE_TEXTS_LENGTH];//
   const char rx_only[LANGUAGE_TEXTS_LENGTH];//
   const char out_of_band[LANGUAGE_TEXTS_LENGTH];//
   const char timeout[LANGUAGE_TEXTS_LENGTH];//
   const char tg_entry[LANGUAGE_TEXTS_LENGTH];//
   const char pc_entry[LANGUAGE_TEXTS_LENGTH];//
   const char user_dmr_id[LANGUAGE_TEXTS_LENGTH];//
   const char contact[LANGUAGE_TEXTS_LENGTH];//
   const char accept_call[LANGUAGE_TEXTS_LENGTH];//  "Accept call?"
   const char private_call[LANGUAGE_TEXTS_LENGTH];// "Private call"
   const char squelch[LANGUAGE_TEXTS_LENGTH];// "Squelch"
   const char quick_menu[LANGUAGE_TEXTS_LENGTH];//"Quick Menu"
   const char filter[LANGUAGE_TEXTS_LENGTH];//"Filter:%s"
   const char all_channels[LANGUAGE_TEXTS_LENGTH];//"All Channels"
   const char gotoChannel[LANGUAGE_TEXTS_LENGTH];//"Goto %d"
   const char scan[LANGUAGE_TEXTS_LENGTH];// "Scan"
   const char channelToVfo[LANGUAGE_TEXTS_LENGTH];// "Channel --> VFO",
   const char vfoToChannel[LANGUAGE_TEXTS_LENGTH];// "VFO --> Channel",
   const char vfoToNewChannel[LANGUAGE_TEXTS_LENGTH];// "VFO --> New Chan",
   const char group[LANGUAGE_TEXTS_LENGTH];//"Group",
   const char private[LANGUAGE_TEXTS_LENGTH];//"Private",
   const char all[LANGUAGE_TEXTS_LENGTH];//"All",
   const char type[LANGUAGE_TEXTS_LENGTH];//"Type:"
   const char timeSlot[LANGUAGE_TEXTS_LENGTH];//"Timeslot"
   const char none[LANGUAGE_TEXTS_LENGTH];//"none"
   const char contact_saved[LANGUAGE_TEXTS_LENGTH];// "Contact saved",
   const char duplicate[LANGUAGE_TEXTS_LENGTH];//"Duplicate"
   const char tg[LANGUAGE_TEXTS_LENGTH];//"TG"
   const char pc[LANGUAGE_TEXTS_LENGTH];//"PC"
   const char ts[LANGUAGE_TEXTS_LENGTH];//"TS"
   const char mode[LANGUAGE_TEXTS_LENGTH];//"Mode"
   const char colour_code[LANGUAGE_TEXTS_LENGTH];//"Color Code"
   const char n_a[LANGUAGE_TEXTS_LENGTH];//"N/A"
   const char bandwidth[LANGUAGE_TEXTS_LENGTH];
   const char stepFreq[LANGUAGE_TEXTS_LENGTH];
   const char tot[LANGUAGE_TEXTS_LENGTH];
   const char off[LANGUAGE_TEXTS_LENGTH];
   const char zone_skip[LANGUAGE_TEXTS_LENGTH];
   const char all_skip[LANGUAGE_TEXTS_LENGTH];
   const char yes[LANGUAGE_TEXTS_LENGTH];
   const char no[LANGUAGE_TEXTS_LENGTH];
   const char tg_list[LANGUAGE_TEXTS_LENGTH];
   const char on[LANGUAGE_TEXTS_LENGTH];
   const char timeout_beep[LANGUAGE_TEXTS_LENGTH];
   const char list_full[LANGUAGE_TEXTS_LENGTH];
   const char dmr_cc_scan[LANGUAGE_TEXTS_LENGTH];
   const char band_limits[LANGUAGE_TEXTS_LENGTH];
   const char beep_volume[LANGUAGE_TEXTS_LENGTH];
   const char dmr_mic_gain[LANGUAGE_TEXTS_LENGTH];
   const char fm_mic_gain[LANGUAGE_TEXTS_LENGTH];
   const char key_long[LANGUAGE_TEXTS_LENGTH];
   const char key_repeat[LANGUAGE_TEXTS_LENGTH];
   const char dmr_filter_timeout[LANGUAGE_TEXTS_LENGTH];
   const char brightness[LANGUAGE_TEXTS_LENGTH];
   const char brightness_off[LANGUAGE_TEXTS_LENGTH];
   const char contrast[LANGUAGE_TEXTS_LENGTH];
   const char screen_invert[LANGUAGE_TEXTS_LENGTH];// for display background
   const char screen_normal[LANGUAGE_TEXTS_LENGTH];// for display background
   const char backlight_timeout[LANGUAGE_TEXTS_LENGTH];
   const char scan_delay[LANGUAGE_TEXTS_LENGTH];
   const char yes___in_uppercase[LANGUAGE_TEXTS_LENGTH];
   const char no___in_uppercase[LANGUAGE_TEXTS_LENGTH];
   const char DISMISS[LANGUAGE_TEXTS_LENGTH];
   const char scan_mode[LANGUAGE_TEXTS_LENGTH];
   const char hold[LANGUAGE_TEXTS_LENGTH];
   const char pause[LANGUAGE_TEXTS_LENGTH];
   const char list_empty[LANGUAGE_TEXTS_LENGTH];
   const char delete_contact_qm[LANGUAGE_TEXTS_LENGTH];
   const char contact_deleted[LANGUAGE_TEXTS_LENGTH];
   const char contact_used[LANGUAGE_TEXTS_LENGTH];
   const char in_tg_list[LANGUAGE_TEXTS_LENGTH];
   const char select_tx[LANGUAGE_TEXTS_LENGTH];
   const char edit_contact[LANGUAGE_TEXTS_LENGTH];
   const char delete_contact[LANGUAGE_TEXTS_LENGTH];
   const char group_call[LANGUAGE_TEXTS_LENGTH];
   const char all_call[LANGUAGE_TEXTS_LENGTH];
   const char tone_scan[LANGUAGE_TEXTS_LENGTH];
   const char low_battery[LANGUAGE_TEXTS_LENGTH];
   const char Auto[LANGUAGE_TEXTS_LENGTH];
   const char manual[LANGUAGE_TEXTS_LENGTH];
   const char ptt_toggle[LANGUAGE_TEXTS_LENGTH];
   const char private_call_handling[LANGUAGE_TEXTS_LENGTH];
   const char stop[LANGUAGE_TEXTS_LENGTH];
   const char one_line[LANGUAGE_TEXTS_LENGTH];
   const char two_lines[LANGUAGE_TEXTS_LENGTH];
   const char new_channel[LANGUAGE_TEXTS_LENGTH];
   const char priority_order[LANGUAGE_TEXTS_LENGTH];
   const char dmr_beep[LANGUAGE_TEXTS_LENGTH];
   const char start[LANGUAGE_TEXTS_LENGTH];
   const char both[LANGUAGE_TEXTS_LENGTH];
   const char vox_threshold[LANGUAGE_TEXTS_LENGTH];
   const char vox_tail[LANGUAGE_TEXTS_LENGTH];
   const char audio_prompt[LANGUAGE_TEXTS_LENGTH];
   const char silent[LANGUAGE_TEXTS_LENGTH];
   const char rx_beep[LANGUAGE_TEXTS_LENGTH];
   const char beep[LANGUAGE_TEXTS_LENGTH];
   const char voice_prompt_level_1[LANGUAGE_TEXTS_LENGTH];
   const char transmitTalkerAliasTS1[LANGUAGE_TEXTS_LENGTH];
   const char squelch_VHF[LANGUAGE_TEXTS_LENGTH];
   const char squelch_220[LANGUAGE_TEXTS_LENGTH];
   const char squelch_UHF[LANGUAGE_TEXTS_LENGTH];
   const char display_screen_invert[LANGUAGE_TEXTS_LENGTH];
   const char openGD77[LANGUAGE_TEXTS_LENGTH];
   const char talkaround[LANGUAGE_TEXTS_LENGTH];
   const char APRS[LANGUAGE_TEXTS_LENGTH];
   const char no_keys[LANGUAGE_TEXTS_LENGTH];
   const char gitCommit[LANGUAGE_TEXTS_LENGTH];
   const char voice_prompt_level_2[LANGUAGE_TEXTS_LENGTH];
   const char voice_prompt_level_3[LANGUAGE_TEXTS_LENGTH];
   const char dmr_filter[LANGUAGE_TEXTS_LENGTH];
   const char talker[LANGUAGE_TEXTS_LENGTH];
   const char dmr_ts_filter[LANGUAGE_TEXTS_LENGTH];
   const char dtmf_contact_list[LANGUAGE_TEXTS_LENGTH];// Menu number 18
   const char channel_power[LANGUAGE_TEXTS_LENGTH];// "Ch Power" for the Channel details screen
   const char from_master[LANGUAGE_TEXTS_LENGTH];// "Master" for the power or squelch setting on the Channel details screen
   const char set_quickkey[LANGUAGE_TEXTS_LENGTH];
   const char dual_watch[LANGUAGE_TEXTS_LENGTH];
   const char info[LANGUAGE_TEXTS_LENGTH];
   const char pwr[LANGUAGE_TEXTS_LENGTH];
   const char user_power[LANGUAGE_TEXTS_LENGTH];
   const char temperature[LANGUAGE_TEXTS_LENGTH];
   const char celcius[LANGUAGE_TEXTS_LENGTH];
   const char seconds[LANGUAGE_TEXTS_LENGTH];
   const char radio_info[LANGUAGE_TEXTS_LENGTH];
   const char temperature_calibration[LANGUAGE_TEXTS_LENGTH];
   const char pin_code[LANGUAGE_TEXTS_LENGTH];
   const char please_confirm[LANGUAGE_TEXTS_LENGTH];
   const char vfo_freq_bind_mode[LANGUAGE_TEXTS_LENGTH];
   const char overwrite_qm[LANGUAGE_TEXTS_LENGTH];
   const char eco_level[LANGUAGE_TEXTS_LENGTH];// Economy / Power saving level
   const char buttons[LANGUAGE_TEXTS_LENGTH];
   const char leds[LANGUAGE_TEXTS_LENGTH];
   const char scan_dwell_time[LANGUAGE_TEXTS_LENGTH];
   const char battery_calibration[LANGUAGE_TEXTS_LENGTH];
   const char low[LANGUAGE_TEXTS_LENGTH];
   const char high[LANGUAGE_TEXTS_LENGTH];
   const char dmr_id[LANGUAGE_TEXTS_LENGTH];
   const char scan_on_boot[LANGUAGE_TEXTS_LENGTH];
   const char dtmf_entry[LANGUAGE_TEXTS_LENGTH];
   const char name[LANGUAGE_TEXTS_LENGTH];
   const char carrier[LANGUAGE_TEXTS_LENGTH];
   const char zone_empty[LANGUAGE_TEXTS_LENGTH];
   const char time[LANGUAGE_TEXTS_LENGTH];
   const char uptime[LANGUAGE_TEXTS_LENGTH];
   const char hours[LANGUAGE_TEXTS_LENGTH];
   const char minutes[LANGUAGE_TEXTS_LENGTH];
   const char satellite[LANGUAGE_TEXTS_LENGTH];
   const char alarm_time[LANGUAGE_TEXTS_LENGTH];
   const char location[LANGUAGE_TEXTS_LENGTH];
   const char date[LANGUAGE_TEXTS_LENGTH];
   const char timeZone[LANGUAGE_TEXTS_LENGTH];
   const char suspend[LANGUAGE_TEXTS_LENGTH];
   const char pass[LANGUAGE_TEXTS_LENGTH];
   const char elevation[LANGUAGE_TEXTS_LENGTH];
   const char azimuth[LANGUAGE_TEXTS_LENGTH];
   const char inHHMMSS[LANGUAGE_TEXTS_LENGTH];
   const char predicting[LANGUAGE_TEXTS_LENGTH];
   const char maximum[LANGUAGE_TEXTS_LENGTH];
   const char satellite_short[LANGUAGE_TEXTS_LENGTH];
   const char local[LANGUAGE_TEXTS_LENGTH];// For timezone
   const char UTC[LANGUAGE_TEXTS_LENGTH];// For timezone
   const char symbols[LANGUAGE_TEXTS_LENGTH]; // NSEW
   const char not_set[LANGUAGE_TEXTS_LENGTH];// Used when Location etc has not been set.
   const char general_options[LANGUAGE_TEXTS_LENGTH];
   const char radio_options[LANGUAGE_TEXTS_LENGTH];
   const char auto_night[LANGUAGE_TEXTS_LENGTH];
   const char dmr_rx_agc[LANGUAGE_TEXTS_LENGTH];
   const char speaker_click_suppress[LANGUAGE_TEXTS_LENGTH];  // MD-9600 ONLY
   const char gps[LANGUAGE_TEXTS_LENGTH];
   const char end_only[LANGUAGE_TEXTS_LENGTH];
   const char dmr_crc[LANGUAGE_TEXTS_LENGTH];
   const char eco[LANGUAGE_TEXTS_LENGTH];
   const char safe_power_on[LANGUAGE_TEXTS_LENGTH];
   const char auto_power_off[LANGUAGE_TEXTS_LENGTH];
   const char apo_with_rf[LANGUAGE_TEXTS_LENGTH];
   const char brightness_night[LANGUAGE_TEXTS_LENGTH];
   const char freq_set_VHF[LANGUAGE_TEXTS_LENGTH];
   const char gps_acquiring[LANGUAGE_TEXTS_LENGTH];
   const char altitude[LANGUAGE_TEXTS_LENGTH];
   const char calibration[LANGUAGE_TEXTS_LENGTH];            //Calibration Menu (204)
   const char freq_set_UHF[LANGUAGE_TEXTS_LENGTH];
   const char cal_frequency[LANGUAGE_TEXTS_LENGTH];
   const char cal_pwr[LANGUAGE_TEXTS_LENGTH];
   const char pwr_set[LANGUAGE_TEXTS_LENGTH];
   const char factory_reset[LANGUAGE_TEXTS_LENGTH];
   const char rx_tune[LANGUAGE_TEXTS_LENGTH];               //MD-9600 only
   const char transmitTalkerAliasTS2[LANGUAGE_TEXTS_LENGTH];
   const char ta_text[LANGUAGE_TEXTS_LENGTH];// "Text"
   const char daytime_theme_day[LANGUAGE_TEXTS_LENGTH];
   const char daytime_theme_night[LANGUAGE_TEXTS_LENGTH];
   const char theme_chooser[LANGUAGE_TEXTS_LENGTH];
   const char theme_options[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_default[LANGUAGE_TEXTS_LENGTH];
   const char theme_bg[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_decoration[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_text_input[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_splashscreen[LANGUAGE_TEXTS_LENGTH];
   const char theme_bg_splashscreen[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_notification[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_warning_notification[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_error_notification[LANGUAGE_TEXTS_LENGTH];
   const char theme_bg_notification[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_menu_name[LANGUAGE_TEXTS_LENGTH];
   const char theme_bg_menu_name[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_menu_item[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_menu_item_selected[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_options_value[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_header_text[LANGUAGE_TEXTS_LENGTH];
   const char theme_bg_header_text[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_rssi_bar[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_rssi_bar_s9p[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_channel_name[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_channel_contact[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_channel_contact_info[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_zone_name[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_rx_freq[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_tx_freq[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_css_sql_values[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_tx_counter[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_polar_drawing[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_satellite_colour[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_gps_number[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_gps_colour[LANGUAGE_TEXTS_LENGTH];
   const char theme_fg_bd_colour[LANGUAGE_TEXTS_LENGTH];
   const char theme_colour_picker_red[LANGUAGE_TEXTS_LENGTH];
   const char theme_colour_picker_green[LANGUAGE_TEXTS_LENGTH];
   const char theme_colour_picker_blue[LANGUAGE_TEXTS_LENGTH];
   const char volume[LANGUAGE_TEXTS_LENGTH];
   const char distance_sort[LANGUAGE_TEXTS_LENGTH];
   const char show_distance[LANGUAGE_TEXTS_LENGTH];
   const char aprs_options[LANGUAGE_TEXTS_LENGTH];
   const char aprs_smart[LANGUAGE_TEXTS_LENGTH];
   const char aprs_channel[LANGUAGE_TEXTS_LENGTH];
   const char aprs_decay[LANGUAGE_TEXTS_LENGTH];
   const char aprs_compress[LANGUAGE_TEXTS_LENGTH];
   const char aprs_interval[LANGUAGE_TEXTS_LENGTH];
   const char aprs_message_interval[LANGUAGE_TEXTS_LENGTH];
   const char aprs_slow_rate[LANGUAGE_TEXTS_LENGTH];
   const char aprs_fast_rate[LANGUAGE_TEXTS_LENGTH];
   const char aprs_low_speed[LANGUAGE_TEXTS_LENGTH];
   const char aprs_high_speed[LANGUAGE_TEXTS_LENGTH];
   const char aprs_turn_angle[LANGUAGE_TEXTS_LENGTH];
   const char aprs_turn_slope[LANGUAGE_TEXTS_LENGTH];
   const char aprs_turn_time[LANGUAGE_TEXTS_LENGTH];
   const char auto_lock[LANGUAGE_TEXTS_LENGTH];
   const char trackball[LANGUAGE_TEXTS_LENGTH];
   const char dmr_force_dmo[LANGUAGE_TEXTS_LENGTH];
   const char games[LANGUAGE_TEXTS_LENGTH];
   const char aprsLog[LANGUAGE_TEXTS_LENGTH];
   const char userManual[LANGUAGE_TEXTS_LENGTH]; // main menu entry, string 276
   const char channelZone[LANGUAGE_TEXTS_LENGTH]; // main menu entry, string 277
   const char fiveToneContacts[LANGUAGE_TEXTS_LENGTH]; // Contacts menu entry, string 278
} stringsTable_t;

#endif // _OPENGD77_UILANGUAGE_H_
