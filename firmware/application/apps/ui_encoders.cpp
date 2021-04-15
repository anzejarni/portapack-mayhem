/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_encoders.hpp"

#include "baseband_api.hpp"
#include "string_format.hpp"
#include "file.hpp"

using namespace portapack;

namespace ui {

static WORKING_AREA(db_thread_wa, 32768);

static msg_t db_thread_fn(void * arg) {
	EncodersView * arg_c = (EncodersView*)arg;	
	chRegSetThreadName("db_thread_fn");

	arg_c->scan_index = 0;		 //Scanning, number of bits in debruijn sequence
	arg_c->bits_per_packet = 0; //Determine the A (Addresses) bit quantity
	for (uint8_t c = 0; c < arg_c->encoder_def->word_length; c++)
		if (arg_c->encoder_def->word_format[c] == 'A') //Address bit found
			arg_c->bits_per_packet++;

	char* charset = (char*) arg_c->encoder_def->address_symbols;

	uint16_t k = (uint16_t) strlen(arg_c->encoder_def->address_symbols);
	uint32_t maxlen = pow((uint32_t)k, arg_c->bits_per_packet);

	uint8_t a[k * arg_c->bits_per_packet];
	memset(a, 0, k * arg_c->bits_per_packet);
	
	//Clear temporary array for codes
	arg_c->db_tmp_pos = 0; 
	memset(arg_c->db_tmp, 0, 32); 

	arg_c->db_codes_done = 0;
	arg_c->db_codes_total = maxlen/arg_c->bits_per_packet;

	arg_c->tx_mode = arg_c->SCAN;
	arg_c->tx_view.set_transmitting(true);
	transmitter_model.set_sampling_rate(OOK_SAMPLERATE);
	transmitter_model.set_rf_amp(true);
	transmitter_model.set_baseband_bandwidth(1750000);
	transmitter_model.enable();

	//std::string file_path = "DEBRUIJN.TXT";
	//auto result = log_file.append(file_path); 
	arg_c->db_seq.init();
	arg_c->db_seq.db_callback(1, 1, arg_c->bits_per_packet, maxlen, 0, k, a, charset, std::bind(&EncodersView::db_callback, arg_c, std::placeholders::_1, std::placeholders::_2));
	transmitter_model.disable();
	arg_c->tx_view.set_transmitting(false);
	arg_c->button_scan.set_text("Scan");
	arg_c->tx_mode = arg_c->IDLE;
	
	chThdExit(0);	
	return 0;
}

void EncodersView::db_callback(char* seq_part, uint8_t seq_part_len) {
	if (chThdShouldTerminate())
		db_seq.abort();

	for (uint8_t i = 0; i < seq_part_len; i++)
	{
		db_tmp[db_tmp_pos] = seq_part[i];
		if (db_tmp_pos == bits_per_packet-1)
		{		
			//log_file.write_line(db_tmp);
			generate_frame(true);	

			//Send
			size_t bitstream_length = make_bitstream(frame_fragments);

			//Check for delay between transmissions
			if (scan_delay.value() > 0) {
				transmitter_model.disable();
				chThdSleepMilliseconds(scan_delay.value());
				transmitter_model.enable();
			}

			baseband::set_ook_data(
				bitstream_length,
				samples_per_bit(),
				afsk_repeats,
				pause_symbols());

			db_codes_done++;
			
			text_status.set(
				to_string_dec_uint(db_codes_done) + "/" +
				to_string_dec_uint(db_codes_total)
			);

			progressbar.set_max(db_codes_total);
			progressbar.set_value(db_codes_done);

			chThdSleepMilliseconds(1);			

			//Clear tmp
			db_tmp_pos = 0; 			
			memset(db_tmp, 0, 32);

		} else {
			db_tmp_pos++;
		}
	}
}

void EncodersView::on_type_change(size_t index) {
	std::string format_string = "";
	size_t word_length;
	char symbol_type;

	encoder_def = &encoder_defs[index];

	field_clk.set_value(encoder_def->default_speed / 1000);
	
	// SymField setup
	word_length = encoder_def->word_length;
	symfield_word.set_length(word_length);
	size_t n = 0, i = 0;
	while (n < word_length) {
		symbol_type = encoder_def->word_format[i++];
		if (symbol_type == 'A') {
			symfield_word.set_symbol_list(n++, encoder_def->address_symbols);
			format_string += 'A';
		} else if (symbol_type == 'D') {
			symfield_word.set_symbol_list(n++, encoder_def->data_symbols);
			format_string += 'D';
		}
	}
	
	// Ugly :( Pad to erase
	format_string.append(24 - format_string.size(), ' ');
	
	text_format.set(format_string);

	generate_frame(false);
}

void EncodersView::draw_waveform() {
	size_t length = frame_fragments.length();

	for (size_t n = 0; n < length; n++)
		waveform_buffer[n] = (frame_fragments[n] == '0') ? 0 : 1;
	
	waveform.set_length(length);
	waveform.set_dirty();
}

void EncodersView::generate_frame(bool is_debruijn) {
	uint8_t i = 0;
	uint8_t pos = 0; //db_tmp[pos] contains the char to send from debruijn sequence 
	char * word_ptr = (char*)encoder_def->word_format;

	frame_fragments.clear();

	while (*word_ptr) {
		if (*word_ptr == 'S')
			frame_fragments += encoder_def->sync;
		else if (*word_ptr == 'D')
			frame_fragments += encoder_def->bit_format[symfield_word.get_sym(i++)]; //Get_sym brings the index of the char chosen in the symfield, so 0, 1 or eventually 2
		else
		{
			if (!is_debruijn) //single tx
				frame_fragments += encoder_def->bit_format[symfield_word.get_sym(i++)]; //Get the address from user's configured symfield
			else 			
			{	//De BRuijn!
				switch (db_tmp[pos++]) {
					case '0':
						frame_fragments += encoder_def->bit_format[0];
						break;
					case '1':
						frame_fragments += encoder_def->bit_format[1];
						break;
					case 'F':
						frame_fragments += encoder_def->bit_format[2];
						break;
				}
				i++; //Even while grabbing this address bit from debruijn, must move forward on the symfield, in case there is a 'D' further ahead
			}	
		}
		word_ptr++;
	}
	draw_waveform();
}

uint32_t EncodersView::samples_per_bit() {
	return OOK_SAMPLERATE / ((field_clk.value() * 1000) / encoder_def->clk_per_fragment);
}

uint32_t EncodersView::pause_symbols() {
	return encoder_def->pause_symbols;
}


void EncodersView::focus() {
	options_enctype.set_selected_index(0);
	on_type_change(0);
	options_enctype.focus();
}

EncodersView::~EncodersView() {
	transmitter_model.disable();
	baseband::shutdown();
}

void EncodersView::update_progress() {
	text_status.set("            "); //euquiq: it was commented

	if (tx_mode == SINGLE) {
			text_status.set(to_string_dec_uint(repeat_index) + "/" + to_string_dec_uint(afsk_repeats));
			progressbar.set_value(repeat_index);
		}
		else if (tx_mode == SCAN)
		{
			/*
			text_status.set(
				to_string_dec_uint(repeat_index) + "/" +
				to_string_dec_uint(afsk_repeats) + " " +
				to_string_dec_uint(scan_progress) + "/" +
				to_string_dec_uint(scan_count)
			);
			progressbar.set_value(scan_progress);
			*/
		}
		else
		{
			text_status.set("Ready");
			progressbar.set_value(0);
		}
	}

void EncodersView::on_tx_progress(const uint32_t progress, const bool done)	{
	if (!done)
	{ // Repeating...
		repeat_index = progress + 1;

		if (tx_mode == SCAN)
		{
			/*
			scan_progress++;
			update_progress();
			*/
		}
		else
		{
			update_progress();
		}
	}
	else
	{ // Done transmitting
		if ((tx_mode == SCAN) && (db_codes_done < db_codes_total))
		{
			/*
			transmitter_model.disable();
			if (abort_scan)
			{ // Kill scan process
				strcpy(str, "Abort");
				text_status.set(str);
				progressbar.set_value(0);
				tx_mode = IDLE;
				abort_scan = false;
				button_scan.set_text("DE BRUIJN TX");
			}
			else
			{
				// Next address
				
				scan_index += bits_per_packet; //Bit index on the debruijn sequence
				scan_progress++;
				repeat_index = 1;
				update_progress();
				start_tx(true);
				
			}
			*/
		}
		else
		{
			transmitter_model.disable();
			tx_mode = IDLE;
			text_status.set("Done");
			progressbar.set_value(0);
			//button_scan.set_text("DE BRUIJN TX"); //again ... if finished scan
			tx_view.set_transmitting(false);
		}
	}
}



	void EncodersView::start_scan() {	
		db_thread = chThdCreateStatic(db_thread_wa, sizeof(db_thread_wa), NORMALPRIO + 10, db_thread_fn, this);
	}

	void EncodersView::start_tx(const bool scan) {
		(void)scan;
		tx_mode = SINGLE;
		repeat_index = 1;
		afsk_repeats = encoder_def->repeat_min;
		progressbar.set_max(afsk_repeats);
		update_progress();
		generate_frame(false);

		size_t bitstream_length = make_bitstream(frame_fragments);

		transmitter_model.set_sampling_rate(OOK_SAMPLERATE);
		transmitter_model.set_rf_amp(true);
		transmitter_model.set_baseband_bandwidth(1750000);
		transmitter_model.enable();

		baseband::set_ook_data(
			bitstream_length,
			samples_per_bit(),
			afsk_repeats,
			pause_symbols());	
	}

	EncodersView::EncodersView(
		NavigationView & nav) : nav_{nav}
	{
		baseband::run_image(portapack::spi_flash::image_tag_ook);

		using option_t = std::pair<std::string, int32_t>;
		std::vector<option_t> enc_options;
		size_t i;
		
		encoder_def = &encoder_defs[0]; // Default encoder def

		add_children({	
						&labels,
						&options_enctype,
						&field_clk,
						&field_frameduration,
						&scan_delay,
						&symfield_word,
						&text_format,
						&waveform,
						&text_status,
						&button_scan,
						&progressbar,
						&tx_view
					});

		// Load encoder types in option field
		for (i = 0; i < ENC_TYPES_COUNT; i++)
			enc_options.emplace_back(std::make_pair(encoder_defs[i].name, i));
		
		options_enctype.on_change = [this](size_t index, int32_t) {
			on_type_change(index);
		};
		
		options_enctype.set_options(enc_options);
		options_enctype.set_selected_index(0);
		
		symfield_word.on_change = [this]() {
			generate_frame(false);
		};
		
		// Selecting input clock changes symbol and word duration
		field_clk.on_change = [this](int32_t value) {
			// value is in kHz, new_value is in us
			int32_t new_value = (encoder_def->clk_per_symbol * 1000000) / (value * 1000);
			if (new_value != field_frameduration.value())
				field_frameduration.set_value(new_value * encoder_def->word_length, false);
		};
		
		// Selecting word duration changes input clock and symbol duration
		field_frameduration.on_change = [this](int32_t value) {
			// value is in us, new_value is in kHz
			int32_t new_value = (value * 1000) / (encoder_def->word_length * encoder_def->clk_per_symbol);
			if (new_value != field_clk.value())
				field_clk.set_value(1000000 / new_value, false);
		};

		tx_view.on_edit_frequency = [this, &nav]() {
			auto new_view = nav.push<FrequencyKeypadView>(transmitter_model.tuning_frequency());
			new_view->on_changed = [this](rf::Frequency f) {
				transmitter_model.set_tuning_frequency(f);
			};
		};

		tx_view.on_start = [this]() {
			tx_view.set_transmitting(true);
			start_tx(false);
		};

		tx_view.on_stop = [this]() {
			tx_view.set_transmitting(false);
			if (tx_mode == SCAN)
			{
				if (db_thread) {
					//arg_c->db_seq.abort();
					chThdTerminate(db_thread);
				}
				transmitter_model.disable();
				tx_view.set_transmitting(false);
				button_scan.set_text("Scan");
				tx_mode = IDLE;
			}
		};

		button_scan.on_select = [this](Button &) {
			if (tx_mode == IDLE)
			{
				start_scan();
				button_scan.set_text("Abort");
				tx_view.set_transmitting(true);
			}
			else if (tx_mode == SCAN)
			{
				if (db_thread) {
					//arg_c->db_seq.abort();
					chThdTerminate(db_thread);
				}
				transmitter_model.disable();
				tx_view.set_transmitting(false);
				button_scan.set_text("Scan");
				tx_mode = IDLE;
			}
		};
	}
} /* namespace ui */