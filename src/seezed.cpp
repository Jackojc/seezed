#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <array>
#include <deque>
#include <thread>
#include <mutex>
#include <cmath>
#include <limits>

#include <libremidi/libremidi.hpp>
#include <libremidi/reader.hpp>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

// ---

#define CZ_RESET "\x1b[0m"
#define CZ_BOLD  "\x1b[1m"

#define CZ_BLACK   "\x1b[30m"
#define CZ_RED     "\x1b[31m"
#define CZ_GREEN   "\x1b[32m"
#define CZ_YELLOW  "\x1b[33m"
#define CZ_BLUE    "\x1b[34m"
#define CZ_MAGENTA "\x1b[35m"
#define CZ_CYAN    "\x1b[36m"
#define CZ_WHITE   "\x1b[37m"

#define SYSEX_MESSAGES \
	X(SYSEX_START, 0xf0) \
	X(SYSEX_END, 0xf7) \
\
	X(CZ_BEND_RANGE, 0x40) \
	X(CZ_TRANSPOSE, 0x41) \
	X(CZ_TONE_MIX, 0x42) \
	X(CZ_GLIDE_NOTE, 0x43) \
	X(CZ_GLIDE_TIME, 0x44) \
	X(CZ_MOD_WHEEL_DEPTH, 0x45) \
	X(CZ_LEVEL, 0x46) \
	X(CZ_GLIDE_STATE, 0x47) \
	X(CZ_PORTAMENTO_SWEEP, 0x48) \
	X(CZ_MODULATION_STATE, 0x49) \
	X(CZ_MOD_AFTER_TOUCH_DEPTH, 0x4a) \
	X(CZ_AMP_AFTER_TOUCH_RANGE, 0x4b) \
	X(CZ_CARTRIDGE_STATE, 0x4c) \
	X(CZ_ONE_MODE, 0x4d) \
	X(CZ_CURSOR, 0x4e) \
	X(CZ_PAGE, 0x4f) \
\
	X(CZ_MULTI_CHANNEL_STATE, 0x50) \
	X(CZ_NUMBER_OF_POLY, 0x51) \
	X(CZ_TONE_2_PITCH, 0x52) \
	X(CZ_SPLIT_POINT, 0x53) \
	X(CZ_SUS_PEDAL_STATE, 0x54) \
	X(CZ_OCTAVE_SHIFT, 0x55) \
	X(CZ_CHORUS_STATE, 0x56) \
	X(CZ_TIME_BREAK_1, 0x57) \
	X(CZ_TIME_BREAK_2, 0x58) \
	X(CZ_KEY_CODE_SWEEP, 0x59)

#define X(x, y) x = y,

enum {
	SYSEX_MESSAGES
};

#undef X

namespace detail {
#define X(x, y) lookup.at(y) = #x;

	constexpr decltype(auto) generate_sysex_table() {
		std::array<std::string_view, std::numeric_limits<uint8_t>::max()> lookup;
		lookup.fill("UNKNOWN");
		SYSEX_MESSAGES
		return lookup;
	}

	constexpr std::string_view sysex_to_string(uint8_t x) {
		return generate_sysex_table().at(x);
	}

#undef X
}  // namespace detail

// ---

constexpr auto SYSEX_FORMAT_STR = CZ_BOLD CZ_BLUE "{}" CZ_RESET " ({:#04x})\n";

// TODO:
// Response doesn't include operation code so we can't parse data this way.
// We should have seperate parsing functions for each response type.
// We _can_ parse request messages this way though so it might be useful
// for decoding patch files saved on disk.
inline libremidi::midi_bytes::iterator parse_sysex(
	libremidi::message& msg, libremidi::midi_bytes::iterator& it, int indent = 0) {
	auto emit = [&]<typename... Ts>(int indent, fmt::format_string<Ts...> format, Ts&&... args) {
		std::string spaces(indent, ' ');

		fmt::print(stderr, "{}", spaces);
		fmt::print(stderr, format, std::forward<Ts>(args)...);
	};

	auto current = *it++;
	emit(indent, SYSEX_FORMAT_STR, detail::sysex_to_string(current), current);

	switch (current) {
		case SYSEX_START: {
			auto id = *it++;

			auto sub1 = *it++;
			auto sub2 = *it++;

			auto channel = *it++ & 0x0f;

			emit(indent + 1, SYSEX_FORMAT_STR, "ID", id);
			emit(indent + 1, SYSEX_FORMAT_STR, "SUB1", sub1);
			emit(indent + 1, SYSEX_FORMAT_STR, "SUB2", sub2);
			emit(indent + 1, SYSEX_FORMAT_STR, "CHANNEL", channel);

			while (it != msg.end() and *it != SYSEX_END) {
				parse_sysex(msg, it, indent + 2);
			}

			auto eox = *it++;
			emit(indent, SYSEX_FORMAT_STR, detail::sysex_to_string(eox), eox);
		} break;

		default: break;
	}

	return it;
}

int main(int argc, const char* argv[]) {
	libremidi::observer obs;

	auto input_ports = obs.get_input_ports();
	auto output_ports = obs.get_output_ports();

	if (argc != 3) {
		std::cerr << "input:\n";
		for (auto& in: input_ports) {
			std::cerr << in.port_name << '\n';
		}

		std::cerr << "output:\n";
		for (auto& out: output_ports) {
			std::cerr << out.port_name << '\n';
		}

		return 1;
	}

	// ---

	auto in = std::find_if(input_ports.begin(), input_ports.end(), [&](auto& x) {
		return x.port_name.find(argv[1]) != std::string::npos;
	});

	if (in == input_ports.end()) {
		std::cerr << "input not found!\n";
		return 1;
	}

	auto out = std::find_if(output_ports.begin(), output_ports.end(), [&](auto& x) {
		return x.port_name.find(argv[2]) != std::string::npos;
	});

	if (out == output_ports.end()) {
		std::cerr << "output not found!\n";
		return 1;
	}

	// ---

	std::deque<libremidi::message> messages;
	std::mutex message_mutex;

	auto midi_callback = [&](libremidi::message message) {
		// fmt::print(stderr, "{::#04x}\n", message);
		fmt::print(stderr, "callback\n");

		std::unique_lock lock { message_mutex };
		messages.push_back(message);
	};

	auto conf =
		libremidi::input_configuration { .on_message = midi_callback, .ignore_sysex = false, .ignore_timing = false };

	// ---

	libremidi::input_port in_port = *in;
	libremidi::output_port out_port = *out;

	libremidi::midi_out midi_out;
	midi_out.open_port(out_port);

	libremidi::midi_in midi_in { conf };
	midi_in.open_port(in_port);

	fmt::print("connected!\n");

	// ---

	auto get = [&]() {
		while (true) {
			std::unique_lock lock { message_mutex };

			if (not messages.empty()) {
				auto v = messages.front();
				messages.pop_front();

				return v;
			}
		}
	};

	midi_out.send_message(libremidi::message {
		0xf0,
		0x44,
		0x00,
		0x00,
		0x70,  // channel
		0x11,  // command
		10,    // patch
		0x70,
		0x31,
		0xf7,
	});

	auto req2 = get();
	fmt::print(stderr, "{::#04x}\n", req2);

	midi_out.send_message(libremidi::message {
		0xf0,
		0x44,
		0x00,
		0x00,
		0x70,  // channel
		0x12,  // command
		10,    // patch
		0x70,
		0x31,
		0xf7,
	});

	fmt::print("A\n");
	auto req3 = get();
	fmt::print(stderr, "{::#04x}\n", req3);

	char input;
	std::cin.get(input);
}
