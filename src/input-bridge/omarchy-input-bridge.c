#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define INPUT_ROOT "/host/input"
#define RESCAN_INTERVAL_MS 5000
#define METRICS_WINDOW_MS 10000
#define METRICS_INTERVAL_BUCKETS 1001
#define MAX_GRABBED_DEVICES 16
#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define BIT_WORD(bit) ((bit) / BITS_PER_LONG)
#define BIT_MASK(bit) (1UL << ((bit) % BITS_PER_LONG))
#define BITSET_LONGS(maximum) (BIT_WORD(maximum) + 1U)

enum device_kind {
	DEVICE_IGNORED = 0,
	DEVICE_KEYBOARD,
	DEVICE_POINTER_RELATIVE,
	DEVICE_POINTER_ABSOLUTE,
};

struct device_match {
	enum device_kind kind;
	bool expected_name;
	char name[256];
	struct input_id id;
};

struct grabbed_device {
	int fd;
	enum device_kind kind;
	dev_t device_number;
	ino_t inode;
	struct input_absinfo abs_x;
	struct input_absinfo abs_y;
	int32_t relative_x;
	int32_t relative_y;
	int32_t absolute_x;
	int32_t absolute_y;
	int32_t wheel;
	int32_t horizontal_wheel;
	int32_t wheel_high_resolution;
	int32_t horizontal_wheel_high_resolution;
	bool has_relative_motion;
	bool has_absolute_x;
	bool has_absolute_y;
	bool has_wheel;
	bool has_horizontal_wheel;
	bool has_wheel_high_resolution;
	bool has_horizontal_wheel_high_resolution;
	bool has_pointer_event;
	bool synchronizing;
	bool monotonic_timestamps;
	uint64_t metrics_motion_frames;
	uint64_t metrics_relative_frames;
	uint64_t metrics_absolute_frames;
	uint64_t metrics_relative_events;
	uint64_t metrics_absolute_events;
	uint64_t metrics_last_frame_ns;
	uint64_t metrics_interval_sum_ns;
	uint64_t metrics_interval_count;
	uint64_t metrics_interval_max_ns;
	uint64_t metrics_interval_histogram[METRICS_INTERVAL_BUCKETS];
	uint64_t metrics_latency_sum_ns;
	uint64_t metrics_latency_count;
	uint64_t metrics_latency_max_ns;
	unsigned long pressed_keys[BITSET_LONGS(KEY_MAX)];
	char path[PATH_MAX];
};

struct grab_state {
	struct grabbed_device devices[MAX_GRABBED_DEVICES];
	size_t count;
	char keyboard_name[256];
	char pointer_name[256];
	char absolute_pointer_name[256];
};

static volatile sig_atomic_t stopping;

struct bridge {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_seat *seat;
	struct zwp_virtual_keyboard_manager_v1 *keyboard_manager;
	struct zwlr_virtual_pointer_manager_v1 *pointer_manager;
	struct zwp_virtual_keyboard_v1 *keyboard;
	struct zwlr_virtual_pointer_v1 *pointer;
	bool metrics_enabled;
	bool metrics_active;
	bool metrics_complete;
	uint64_t metrics_start_ns;
	uint64_t metrics_end_ns;
	uint64_t metrics_motion_requests;
	uint64_t metrics_flush_calls;
	uint64_t metrics_flush_eagain;
};

static void log_error(const char *message)
{
	fprintf(stderr, "omarchy-input-bridge: ERROR: %s\n", message);
}

static bool bit_is_set(const unsigned long *bits, unsigned int bit)
{
	return (bits[BIT_WORD(bit)] & BIT_MASK(bit)) != 0;
}

static void set_bit(unsigned long *bits, unsigned int bit, bool enabled)
{
	if (enabled)
		bits[BIT_WORD(bit)] |= BIT_MASK(bit);
	else
		bits[BIT_WORD(bit)] &= ~BIT_MASK(bit);
}

static const char *device_kind_name(enum device_kind kind)
{
	switch (kind) {
	case DEVICE_KEYBOARD:
		return "keyboard";
	case DEVICE_POINTER_RELATIVE:
		return "pointer-relative";
	case DEVICE_POINTER_ABSOLUTE:
		return "pointer-absolute";
	case DEVICE_IGNORED:
	default:
		return "ignored";
	}
}

static int initialize_expected_names(struct grab_state *state, const char *seat)
{
	int keyboard_length = snprintf(state->keyboard_name,
		sizeof(state->keyboard_name), "Keyboard passthrough (%s)", seat);
	int pointer_length = snprintf(state->pointer_name,
		sizeof(state->pointer_name), "Mouse passthrough (%s)", seat);
	int absolute_length = snprintf(state->absolute_pointer_name,
		sizeof(state->absolute_pointer_name),
		"Mouse passthrough (%s) (absolute)", seat);

	if (keyboard_length < 0 || (size_t)keyboard_length >= sizeof(state->keyboard_name) ||
		pointer_length < 0 || (size_t)pointer_length >= sizeof(state->pointer_name) ||
		absolute_length < 0 ||
		(size_t)absolute_length >= sizeof(state->absolute_pointer_name)) {
		log_error("SUNSHINE_SEAT is too long");
		return -1;
	}
	return 0;
}

static int read_capabilities(int fd, unsigned int event_type,
		unsigned long *bits, size_t size)
{
	memset(bits, 0, size);
	if (ioctl(fd, EVIOCGBIT(event_type, size), bits) < 0)
		return -1;
	return 0;
}

static int inspect_device(int fd, const struct grab_state *state,
		struct device_match *match, const char **reason)
{
	unsigned long event_bits[BITSET_LONGS(EV_MAX)] = {0};
	unsigned long key_bits[BITSET_LONGS(KEY_MAX)] = {0};
	unsigned long relative_bits[BITSET_LONGS(REL_MAX)] = {0};
	unsigned long absolute_bits[BITSET_LONGS(ABS_MAX)] = {0};

	memset(match, 0, sizeof(*match));
	if (ioctl(fd, EVIOCGNAME(sizeof(match->name)), match->name) < 0 ||
		ioctl(fd, EVIOCGID, &match->id) < 0) {
		*reason = "identity-query-failed";
		return -1;
	}

	if (strcmp(match->name, state->keyboard_name) == 0) {
		match->kind = DEVICE_KEYBOARD;
		match->expected_name = true;
	} else if (strcmp(match->name, state->pointer_name) == 0) {
		match->kind = DEVICE_POINTER_RELATIVE;
		match->expected_name = true;
	} else if (strcmp(match->name, state->absolute_pointer_name) == 0) {
		match->kind = DEVICE_POINTER_ABSOLUTE;
		match->expected_name = true;
	} else {
		*reason = "name-not-allowlisted";
		return 0;
	}

	if (match->id.vendor != 0xbeef || match->id.product != 0xdead ||
		match->id.version != 0x0111) {
		*reason = "unexpected-evdev-identity";
		return -1;
	}

	if (read_capabilities(fd, 0, event_bits, sizeof(event_bits)) < 0 ||
		read_capabilities(fd, EV_KEY, key_bits, sizeof(key_bits)) < 0) {
		*reason = "capability-query-failed";
		return -1;
	}

	switch (match->kind) {
	case DEVICE_KEYBOARD:
		if (!bit_is_set(event_bits, EV_KEY) || !bit_is_set(key_bits, KEY_A)) {
			*reason = "missing-keyboard-capabilities";
			return -1;
		}
		break;
	case DEVICE_POINTER_RELATIVE:
		if (read_capabilities(fd, EV_REL, relative_bits,
				sizeof(relative_bits)) < 0 ||
			!bit_is_set(event_bits, EV_KEY) ||
			!bit_is_set(event_bits, EV_REL) ||
			!bit_is_set(key_bits, BTN_LEFT) ||
			!bit_is_set(relative_bits, REL_X) ||
			!bit_is_set(relative_bits, REL_Y)) {
			*reason = "missing-relative-pointer-capabilities";
			return -1;
		}
		break;
	case DEVICE_POINTER_ABSOLUTE:
		if (read_capabilities(fd, EV_ABS, absolute_bits,
				sizeof(absolute_bits)) < 0 ||
			!bit_is_set(event_bits, EV_KEY) ||
			!bit_is_set(event_bits, EV_ABS) ||
			!bit_is_set(key_bits, BTN_LEFT) ||
			!bit_is_set(absolute_bits, ABS_X) ||
			!bit_is_set(absolute_bits, ABS_Y)) {
			*reason = "missing-absolute-pointer-capabilities";
			return -1;
		}
		break;
	case DEVICE_IGNORED:
	default:
		*reason = "internal-classification-error";
		return -1;
	}

	*reason = "allowlisted";
	return 1;
}

static int event_node_filter(const struct dirent *entry)
{
	return strncmp(entry->d_name, "event", 5) == 0;
}

static int open_event_node(const char *path)
{
	return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

static int discover_once(const char *input_root, const char *seat)
{
	struct grab_state state = {0};
	struct dirent **entries = NULL;
	int inspection_failures = 0;

	if (initialize_expected_names(&state, seat) < 0)
		return 1;

	int count = scandir(input_root, &entries, event_node_filter, alphasort);
	if (count < 0) {
		fprintf(stderr, "omarchy-input-bridge: ERROR: cannot scan %s: %s\n",
			input_root, strerror(errno));
		return 1;
	}

	printf("Input discovery under %s (seat discriminator: %s)\n",
		input_root, seat);
	for (int index = 0; index < count; index++) {
		char path[PATH_MAX];
		struct device_match match;
		const char *reason = NULL;
		int path_length = snprintf(path, sizeof(path), "%s/%s",
			input_root, entries[index]->d_name);
		free(entries[index]);
		if (path_length < 0 || (size_t)path_length >= sizeof(path))
			continue;

		int fd = open_event_node(path);
		if (fd < 0) {
			printf("  %s: inaccessible (%s)\n", path, strerror(errno));
			inspection_failures++;
			continue;
		}

		int matched = inspect_device(fd, &state, &match, &reason);
		close(fd);
		printf("  %s: %s name=\"%s\" id=%04x:%04x:%04x reason=%s\n",
			path, matched > 0 ? device_kind_name(match.kind) : "ignored",
			match.name[0] != '\0' ? match.name : "(unavailable)",
			match.id.vendor, match.id.product, match.id.version,
			reason != NULL ? reason : "unknown");
		if (matched < 0)
			inspection_failures++;
	}
	free(entries);

	if (inspection_failures > 0) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: %d event device(s) could not be safely classified\n",
			inspection_failures);
		return 1;
	}
	return 0;
}

static bool device_is_tracked(const struct grab_state *state, const char *path)
{
	for (size_t index = 0; index < state->count; index++) {
		if (strcmp(state->devices[index].path, path) == 0)
			return true;
	}
	return false;
}

static uint32_t monotonic_milliseconds(void)
{
	struct timespec now = {0};
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;
	return (uint32_t)((uint64_t)now.tv_sec * 1000U +
		(uint64_t)now.tv_nsec / 1000000U);
}

static uint64_t monotonic_nanoseconds(void)
{
	struct timespec now = {0};
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void metrics_start_on_motion(struct bridge *bridge, uint64_t now_ns)
{
	if (bridge == NULL || !bridge->metrics_enabled || bridge->metrics_active ||
		bridge->metrics_complete)
		return;
	bridge->metrics_active = true;
	bridge->metrics_start_ns = now_ns;
	bridge->metrics_end_ns = now_ns + (uint64_t)METRICS_WINDOW_MS * 1000000ULL;
	fprintf(stderr,
		"omarchy-input-bridge: metrics capture started on first motion (%d ms)\n",
		METRICS_WINDOW_MS);
}

static uint64_t metrics_percentile_ms(const struct grabbed_device *device,
		unsigned int percentile)
{
	if (device->metrics_interval_count == 0)
		return 0;
	uint64_t target = (device->metrics_interval_count * percentile + 99U) / 100U;
	uint64_t seen = 0;
	for (uint64_t bucket = 0; bucket < METRICS_INTERVAL_BUCKETS; bucket++) {
		seen += device->metrics_interval_histogram[bucket];
		if (seen >= target)
			return bucket;
	}
	return METRICS_INTERVAL_BUCKETS - 1;
}

static void metrics_record_pointer_frame(struct bridge *bridge,
		struct grabbed_device *device, const struct input_event *event,
		uint64_t now_ns)
{
	if (bridge == NULL || !bridge->metrics_active ||
		now_ns > bridge->metrics_end_ns ||
		(!device->has_relative_motion && !device->has_absolute_x &&
		 !device->has_absolute_y))
		return;

	device->metrics_motion_frames++;
	if (device->has_relative_motion)
		device->metrics_relative_frames++;
	if (device->has_absolute_x || device->has_absolute_y)
		device->metrics_absolute_frames++;
	uint64_t event_ns = 0;
	if (device->monotonic_timestamps)
		event_ns = (uint64_t)event->time.tv_sec * 1000000000ULL +
			(uint64_t)event->time.tv_usec * 1000ULL;
	uint64_t frame_ns = event_ns != 0 ? event_ns : now_ns;
	if (device->metrics_last_frame_ns != 0) {
		uint64_t interval = frame_ns >= device->metrics_last_frame_ns ?
			frame_ns - device->metrics_last_frame_ns : 0;
		uint64_t bucket = (interval + 500000ULL) / 1000000ULL;
		if (bucket >= METRICS_INTERVAL_BUCKETS)
			bucket = METRICS_INTERVAL_BUCKETS - 1;
		device->metrics_interval_histogram[bucket]++;
		device->metrics_interval_sum_ns += interval;
		device->metrics_interval_count++;
		if (interval > device->metrics_interval_max_ns)
			device->metrics_interval_max_ns = interval;
	}
	device->metrics_last_frame_ns = frame_ns;

	if (event_ns != 0 && now_ns >= event_ns) {
			uint64_t latency = now_ns - event_ns;
			device->metrics_latency_sum_ns += latency;
			device->metrics_latency_count++;
			if (latency > device->metrics_latency_max_ns)
				device->metrics_latency_max_ns = latency;
	}
}

static void metrics_finish(struct bridge *bridge, const struct grab_state *state,
		uint64_t now_ns)
{
	if (bridge == NULL || !bridge->metrics_active || now_ns < bridge->metrics_end_ns)
		return;

	bridge->metrics_active = false;
	bridge->metrics_complete = true;
	fprintf(stderr,
		"omarchy-input-bridge: metrics result window_ms=%d wayland_motion_requests=%llu flush_calls=%llu flush_eagain=%llu\n",
		METRICS_WINDOW_MS,
		(unsigned long long)bridge->metrics_motion_requests,
		(unsigned long long)bridge->metrics_flush_calls,
		(unsigned long long)bridge->metrics_flush_eagain);
	for (size_t index = 0; index < state->count; index++) {
		const struct grabbed_device *device = &state->devices[index];
		if (device->kind != DEVICE_POINTER_RELATIVE &&
			device->kind != DEVICE_POINTER_ABSOLUTE)
			continue;
		double hz = (double)device->metrics_motion_frames * 1000.0 /
			(double)METRICS_WINDOW_MS;
		double mean_interval_ms = device->metrics_interval_count > 0 ?
			(double)device->metrics_interval_sum_ns /
			(double)device->metrics_interval_count / 1000000.0 : 0.0;
		double mean_latency_ms = device->metrics_latency_count > 0 ?
			(double)device->metrics_latency_sum_ns /
			(double)device->metrics_latency_count / 1000000.0 : 0.0;
		fprintf(stderr,
			"omarchy-input-bridge: metrics device=%s kind=%s motion_frames=%llu rate_hz=%.1f relative_frames=%llu absolute_frames=%llu relative_events=%llu absolute_events=%llu interval_mean_ms=%.2f interval_p50_ms=%llu interval_p95_ms=%llu interval_max_ms=%.2f processing_mean_ms=%.3f processing_max_ms=%.3f timestamp_clock=%s\n",
			device->path, device_kind_name(device->kind),
			(unsigned long long)device->metrics_motion_frames, hz,
			(unsigned long long)device->metrics_relative_frames,
			(unsigned long long)device->metrics_absolute_frames,
			(unsigned long long)device->metrics_relative_events,
			(unsigned long long)device->metrics_absolute_events,
			mean_interval_ms,
			(unsigned long long)metrics_percentile_ms(device, 50),
			(unsigned long long)metrics_percentile_ms(device, 95),
			(double)device->metrics_interval_max_ns / 1000000.0,
			mean_latency_ms,
			(double)device->metrics_latency_max_ns / 1000000.0,
			device->monotonic_timestamps ? "monotonic" : "unavailable");
	}
}

static int bridge_flush(struct bridge *bridge)
{
	if (bridge->metrics_active)
		bridge->metrics_flush_calls++;
	if (wl_display_flush(bridge->display) >= 0)
		return 0;
	if (errno == EAGAIN) {
		if (bridge->metrics_active)
			bridge->metrics_flush_eagain++;
		return 0;
	}
	return -1;
}

static void release_forwarded_state(struct grabbed_device *device,
		struct bridge *bridge)
{
	if (bridge == NULL)
		return;
	uint32_t time = monotonic_milliseconds();
	bool pointer_released = false;

	for (unsigned int code = 0; code <= KEY_MAX; code++) {
		if (!bit_is_set(device->pressed_keys, code))
			continue;
		if (device->kind == DEVICE_KEYBOARD) {
			zwp_virtual_keyboard_v1_key(bridge->keyboard, time, code,
				WL_KEYBOARD_KEY_STATE_RELEASED);
		} else {
			zwlr_virtual_pointer_v1_button(bridge->pointer, time, code,
				WL_POINTER_BUTTON_STATE_RELEASED);
			pointer_released = true;
		}
	}
	if (pointer_released)
		zwlr_virtual_pointer_v1_frame(bridge->pointer);
	if (bridge_flush(bridge) < 0)
		log_error("could not flush input releases to Wayland");
}

static void release_device(struct grab_state *state, size_t index,
		struct bridge *bridge)
{
	struct grabbed_device *device = &state->devices[index];
	release_forwarded_state(device, bridge);
	if (ioctl(device->fd, EVIOCGRAB, 0) < 0 && errno != ENODEV)
		fprintf(stderr, "omarchy-input-bridge: warning: could not release %s: %s\n",
			device->path, strerror(errno));
	close(device->fd);
	fprintf(stderr, "omarchy-input-bridge: released %s (%s)\n",
		device->path, device_kind_name(device->kind));
	state->devices[index] = state->devices[state->count - 1];
	state->count--;
}

static void reconcile_removed_devices(struct grab_state *state,
		struct bridge *bridge)
{
	for (size_t index = 0; index < state->count;) {
		struct stat node;
		if (stat(state->devices[index].path, &node) < 0 ||
			node.st_rdev != state->devices[index].device_number ||
			node.st_ino != state->devices[index].inode)
			release_device(state, index, bridge);
		else
			index++;
	}
}

static void free_remaining_entries(struct dirent **entries, int first, int count)
{
	for (int index = first; index < count; index++)
		free(entries[index]);
	free(entries);
}

static int scan_and_grab(struct grab_state *state, const char *input_root)
{
	struct dirent **entries = NULL;
	int count = scandir(input_root, &entries, event_node_filter, alphasort);
	if (count < 0) {
		fprintf(stderr, "omarchy-input-bridge: ERROR: cannot scan %s: %s\n",
			input_root, strerror(errno));
		return -1;
	}

	for (int index = 0; index < count; index++) {
		char path[PATH_MAX];
		struct device_match match;
		const char *reason = NULL;
		int path_length = snprintf(path, sizeof(path), "%s/%s",
			input_root, entries[index]->d_name);
		free(entries[index]);
		if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
			device_is_tracked(state, path))
			continue;

		int fd = open_event_node(path);
		if (fd < 0 && errno == ENOENT)
			continue;
		if (fd < 0) {
			fprintf(stderr,
				"omarchy-input-bridge: ERROR: cannot inspect %s: %s\n",
				path, strerror(errno));
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}
		int matched = inspect_device(fd, state, &match, &reason);
		if (matched < 0) {
			fprintf(stderr,
				"omarchy-input-bridge: ERROR: cannot safely classify %s: %s\n",
				path, reason);
			close(fd);
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}
		if (matched <= 0) {
			close(fd);
			continue;
		}
		if (state->count >= MAX_GRABBED_DEVICES) {
			log_error("too many allowlisted devices");
			close(fd);
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}
		int event_clock = CLOCK_MONOTONIC;
		bool monotonic_timestamps = ioctl(fd, EVIOCSCLOCKID, &event_clock) == 0;
		if (ioctl(fd, EVIOCGRAB, 1) < 0) {
			fprintf(stderr,
				"omarchy-input-bridge: ERROR: EVIOCGRAB failed for %s: %s\n",
				path, strerror(errno));
			close(fd);
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}

		struct grabbed_device *device = &state->devices[state->count++];
		memset(device, 0, sizeof(*device));
		struct stat descriptor_stat;
		if (fstat(fd, &descriptor_stat) < 0) {
			fprintf(stderr,
				"omarchy-input-bridge: ERROR: fstat failed for %s: %s\n",
				path, strerror(errno));
			ioctl(fd, EVIOCGRAB, 0);
			close(fd);
			state->count--;
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}
		device->fd = fd;
		device->kind = match.kind;
		device->monotonic_timestamps = monotonic_timestamps;
		device->device_number = descriptor_stat.st_rdev;
		device->inode = descriptor_stat.st_ino;
		if (device->kind == DEVICE_POINTER_ABSOLUTE &&
			(ioctl(fd, EVIOCGABS(ABS_X), &device->abs_x) < 0 ||
			 ioctl(fd, EVIOCGABS(ABS_Y), &device->abs_y) < 0 ||
			 device->abs_x.maximum <= device->abs_x.minimum ||
			 device->abs_y.maximum <= device->abs_y.minimum)) {
			fprintf(stderr,
				"omarchy-input-bridge: ERROR: invalid absolute ranges for %s\n",
				path);
			ioctl(fd, EVIOCGRAB, 0);
			close(fd);
			state->count--;
			free_remaining_entries(entries, index + 1, count);
			return -1;
		}
		device->absolute_x = device->abs_x.value;
		device->absolute_y = device->abs_y.value;
		snprintf(device->path, sizeof(device->path), "%s", path);
		fprintf(stderr,
			"omarchy-input-bridge: grabbed %s (%s, %04x:%04x:%04x)\n",
			path, device_kind_name(match.kind), match.id.vendor,
			match.id.product, match.id.version);
	}
	free(entries);
	return 0;
}

static uint32_t event_time_milliseconds(const struct input_event *event)
{
	(void)event;
	return monotonic_milliseconds();
}

static bool pointer_button(unsigned int code)
{
	return code >= BTN_MOUSE && code <= BTN_TASK;
}

static void send_scroll_axis(struct zwlr_virtual_pointer_v1 *pointer,
		uint32_t time, uint32_t axis, int32_t low_resolution,
		bool has_low_resolution, int32_t high_resolution,
		bool has_high_resolution)
{
	if (has_high_resolution) {
		double distance = -(double)high_resolution / 12.0;
		if (has_low_resolution) {
			zwlr_virtual_pointer_v1_axis_discrete(pointer, time, axis,
				wl_fixed_from_double(distance), -low_resolution);
		} else {
			zwlr_virtual_pointer_v1_axis(pointer, time, axis,
				wl_fixed_from_double(distance));
		}
	} else if (has_low_resolution) {
		zwlr_virtual_pointer_v1_axis_discrete(pointer, time, axis,
			wl_fixed_from_double(-10.0 * low_resolution), -low_resolution);
	}
}

static void flush_pointer_frame(struct bridge *bridge,
		struct grabbed_device *device, uint32_t time)
{
	if (device->has_relative_motion) {
		zwlr_virtual_pointer_v1_motion(bridge->pointer, time,
			wl_fixed_from_int(device->relative_x),
			wl_fixed_from_int(device->relative_y));
		if (bridge->metrics_active &&
			monotonic_nanoseconds() <= bridge->metrics_end_ns)
			bridge->metrics_motion_requests++;
	}
	if (device->has_absolute_x || device->has_absolute_y) {
		int32_t absolute_x = device->absolute_x;
		int32_t absolute_y = device->absolute_y;
		if (absolute_x < device->abs_x.minimum)
			absolute_x = device->abs_x.minimum;
		if (absolute_x > device->abs_x.maximum)
			absolute_x = device->abs_x.maximum;
		if (absolute_y < device->abs_y.minimum)
			absolute_y = device->abs_y.minimum;
		if (absolute_y > device->abs_y.maximum)
			absolute_y = device->abs_y.maximum;
		uint32_t x = (uint32_t)(absolute_x - device->abs_x.minimum);
		uint32_t y = (uint32_t)(absolute_y - device->abs_y.minimum);
		uint32_t x_extent = (uint32_t)(device->abs_x.maximum -
			device->abs_x.minimum);
		uint32_t y_extent = (uint32_t)(device->abs_y.maximum -
			device->abs_y.minimum);
		zwlr_virtual_pointer_v1_motion_absolute(bridge->pointer, time,
			x, y, x_extent, y_extent);
		if (bridge->metrics_active &&
			monotonic_nanoseconds() <= bridge->metrics_end_ns)
			bridge->metrics_motion_requests++;
	}
	if (device->has_wheel || device->has_horizontal_wheel ||
		device->has_wheel_high_resolution ||
		device->has_horizontal_wheel_high_resolution)
		zwlr_virtual_pointer_v1_axis_source(bridge->pointer,
			WL_POINTER_AXIS_SOURCE_WHEEL);
	send_scroll_axis(bridge->pointer, time, WL_POINTER_AXIS_VERTICAL_SCROLL,
		device->wheel, device->has_wheel,
		device->wheel_high_resolution,
		device->has_wheel_high_resolution);
	send_scroll_axis(bridge->pointer, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
		device->horizontal_wheel, device->has_horizontal_wheel,
		device->horizontal_wheel_high_resolution,
		device->has_horizontal_wheel_high_resolution);

	if (device->has_pointer_event || device->has_relative_motion ||
		device->has_absolute_x || device->has_absolute_y ||
		device->has_wheel || device->has_horizontal_wheel ||
		device->has_wheel_high_resolution ||
		device->has_horizontal_wheel_high_resolution)
		zwlr_virtual_pointer_v1_frame(bridge->pointer);

	device->relative_x = 0;
	device->relative_y = 0;
	device->wheel = 0;
	device->horizontal_wheel = 0;
	device->wheel_high_resolution = 0;
	device->horizontal_wheel_high_resolution = 0;
	device->has_relative_motion = false;
	device->has_absolute_x = false;
	device->has_absolute_y = false;
	device->has_wheel = false;
	device->has_horizontal_wheel = false;
	device->has_wheel_high_resolution = false;
	device->has_horizontal_wheel_high_resolution = false;
	device->has_pointer_event = false;
}

static void discard_pointer_frame(struct grabbed_device *device)
{
	device->relative_x = 0;
	device->relative_y = 0;
	device->wheel = 0;
	device->horizontal_wheel = 0;
	device->wheel_high_resolution = 0;
	device->horizontal_wheel_high_resolution = 0;
	device->has_relative_motion = false;
	device->has_absolute_x = false;
	device->has_absolute_y = false;
	device->has_wheel = false;
	device->has_horizontal_wheel = false;
	device->has_wheel_high_resolution = false;
	device->has_horizontal_wheel_high_resolution = false;
	device->has_pointer_event = false;
}

static int resynchronize_pressed_keys(struct bridge *bridge,
		struct grabbed_device *device, uint32_t time)
{
	unsigned long current[BITSET_LONGS(KEY_MAX)] = {0};
	bool pointer_changed = false;

	if (ioctl(device->fd, EVIOCGKEY(sizeof(current)), current) < 0)
		return -1;

	for (unsigned int code = 0; code <= KEY_MAX; code++) {
		if (device->kind == DEVICE_KEYBOARD) {
			if (code >= BTN_MISC)
				continue;
		} else if (!pointer_button(code)) {
			continue;
		}
		bool pressed = bit_is_set(current, code);
		if (pressed == bit_is_set(device->pressed_keys, code))
			continue;
		set_bit(device->pressed_keys, code, pressed);
		if (device->kind == DEVICE_KEYBOARD) {
			zwp_virtual_keyboard_v1_key(bridge->keyboard, time, code,
				pressed ? WL_KEYBOARD_KEY_STATE_PRESSED :
				WL_KEYBOARD_KEY_STATE_RELEASED);
		} else {
			zwlr_virtual_pointer_v1_button(bridge->pointer, time, code,
				pressed ? WL_POINTER_BUTTON_STATE_PRESSED :
				WL_POINTER_BUTTON_STATE_RELEASED);
			pointer_changed = true;
		}
	}
	if (pointer_changed)
		zwlr_virtual_pointer_v1_frame(bridge->pointer);
	if (bridge_flush(bridge) < 0)
		return -1;
	return 0;
}

static int forward_input_event(struct bridge *bridge,
		struct grabbed_device *device, const struct input_event *event)
{
	uint32_t time = event_time_milliseconds(event);
	uint64_t now_ns = monotonic_nanoseconds();
	if (event->type == EV_SYN && event->code == SYN_DROPPED) {
		discard_pointer_frame(device);
		device->synchronizing = true;
		fprintf(stderr,
			"omarchy-input-bridge: warning: evdev synchronization lost for %s; resynchronizing\n",
			device->path);
		return 0;
	}
	if (device->synchronizing) {
		if (event->type == EV_SYN && event->code == SYN_REPORT) {
			device->synchronizing = false;
			return resynchronize_pressed_keys(bridge, device, time);
		}
		return 0;
	}

	if (device->kind == DEVICE_KEYBOARD) {
		if (event->type == EV_KEY && event->code < BTN_MISC &&
			event->value <= 1) {
			if (event->code <= KEY_MAX)
				set_bit(device->pressed_keys, event->code, event->value != 0);
			zwp_virtual_keyboard_v1_key(bridge->keyboard, time, event->code,
				event->value != 0 ? WL_KEYBOARD_KEY_STATE_PRESSED :
				WL_KEYBOARD_KEY_STATE_RELEASED);
		} else if (event->type == EV_SYN && event->code == SYN_REPORT) {
			if (bridge_flush(bridge) < 0)
				return -1;
		}
		return 0;
	}

	if (event->type == EV_KEY && pointer_button(event->code) &&
		event->value <= 1) {
		set_bit(device->pressed_keys, event->code, event->value != 0);
		zwlr_virtual_pointer_v1_button(bridge->pointer, time, event->code,
			event->value != 0 ? WL_POINTER_BUTTON_STATE_PRESSED :
			WL_POINTER_BUTTON_STATE_RELEASED);
		device->has_pointer_event = true;
	} else if (event->type == EV_REL) {
		switch (event->code) {
		case REL_X:
			if (event->value != 0)
				metrics_start_on_motion(bridge, now_ns);
			if (bridge->metrics_active && now_ns <= bridge->metrics_end_ns)
				device->metrics_relative_events++;
			device->relative_x += event->value;
			device->has_relative_motion = true;
			break;
		case REL_Y:
			if (event->value != 0)
				metrics_start_on_motion(bridge, now_ns);
			if (bridge->metrics_active && now_ns <= bridge->metrics_end_ns)
				device->metrics_relative_events++;
			device->relative_y += event->value;
			device->has_relative_motion = true;
			break;
		case REL_WHEEL:
			device->wheel += event->value;
			device->has_wheel = true;
			break;
		case REL_HWHEEL:
			device->horizontal_wheel += event->value;
			device->has_horizontal_wheel = true;
			break;
		case REL_WHEEL_HI_RES:
			device->wheel_high_resolution += event->value;
			device->has_wheel_high_resolution = true;
			break;
		case REL_HWHEEL_HI_RES:
			device->horizontal_wheel_high_resolution += event->value;
			device->has_horizontal_wheel_high_resolution = true;
			break;
		default:
			break;
		}
	} else if (event->type == EV_ABS) {
		if (event->code == ABS_X) {
			if (event->value != device->absolute_x)
				metrics_start_on_motion(bridge, now_ns);
			if (bridge->metrics_active && now_ns <= bridge->metrics_end_ns)
				device->metrics_absolute_events++;
			device->absolute_x = event->value;
			device->has_absolute_x = true;
		} else if (event->code == ABS_Y) {
			if (event->value != device->absolute_y)
				metrics_start_on_motion(bridge, now_ns);
			if (bridge->metrics_active && now_ns <= bridge->metrics_end_ns)
				device->metrics_absolute_events++;
			device->absolute_y = event->value;
			device->has_absolute_y = true;
		}
	} else if (event->type == EV_SYN && event->code == SYN_REPORT) {
		metrics_record_pointer_frame(bridge, device, event, now_ns);
		flush_pointer_frame(bridge, device, time);
		if (bridge_flush(bridge) < 0)
			return -1;
	}
	return 0;
}

static int drain_device(struct grabbed_device *device, struct bridge *bridge)
{
	struct input_event events[64];
	ssize_t bytes;

	while ((bytes = read(device->fd, events, sizeof(events))) > 0) {
		if ((size_t)bytes % sizeof(events[0]) != 0) {
			errno = EIO;
			return -1;
		}
		if (bridge == NULL)
			continue;
		size_t count = (size_t)bytes / sizeof(events[0]);
		for (size_t index = 0; index < count; index++) {
			if (forward_input_event(bridge, device, &events[index]) < 0)
				return -1;
		}
	}
	if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;
	return 0;
}

static void handle_signal(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static int publish_ready_file(const char *path)
{
	char temporary[PATH_MAX];
	int length = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path,
		(long)getpid());
	if (length < 0 || (size_t)length >= sizeof(temporary)) {
		log_error("readiness path is too long");
		return -1;
	}
	mode_t old_umask = umask(0077);
	int fd = open(temporary,
		O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	umask(old_umask);
	if (fd < 0) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: cannot publish readiness at %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	static const char ready[] = "ready\n";
	ssize_t written = write(fd, ready, sizeof(ready) - 1);
	int saved_errno = written < 0 ? errno : EIO;
	bool complete = written == (ssize_t)(sizeof(ready) - 1);
	if (complete && fsync(fd) < 0) {
		complete = false;
		saved_errno = errno;
	}
	if (close(fd) < 0 && complete) {
		complete = false;
		saved_errno = errno;
	}
	if (!complete) {
		errno = saved_errno;
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: cannot write readiness at %s: %s\n",
			path, strerror(errno));
		unlink(temporary);
		return -1;
	}
	if (rename(temporary, path) < 0) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: cannot publish readiness at %s: %s\n",
			path, strerror(errno));
		unlink(temporary);
		return -1;
	}
	return 0;
}

static int monitor_devices(const char *input_root, const char *seat,
		struct bridge *bridge, const char *ready_file)
{
	struct grab_state state = {0};
	int inotify_fd = -1;
	int result = 1;
	bool readiness_published = false;
	struct sigaction action = {
		.sa_handler = handle_signal,
	};

	if (initialize_expected_names(&state, seat) < 0)
		return 1;
	if (sigemptyset(&action.sa_mask) < 0 ||
		sigaction(SIGINT, &action, NULL) < 0 ||
		sigaction(SIGTERM, &action, NULL) < 0) {
		log_error("could not install signal handlers");
		return 1;
	}

	inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd < 0 || inotify_add_watch(inotify_fd, input_root,
			IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB) < 0) {
		fprintf(stderr, "omarchy-input-bridge: ERROR: cannot monitor %s: %s\n",
			input_root, strerror(errno));
		goto out;
	}

	fprintf(stderr, "omarchy-input-bridge: %s monitoring %s for seat %s\n",
		bridge != NULL ? "forwarding" : "grab-only mode", input_root, seat);
	if (scan_and_grab(&state, input_root) < 0)
		goto out;
	if (ready_file != NULL) {
		if (publish_ready_file(ready_file) < 0)
			goto out;
		readiness_published = true;
		fprintf(stderr, "omarchy-input-bridge: ready (%s)\n", ready_file);
	}
	bool rescan_requested = false;
	uint64_t next_rescan_ns = monotonic_nanoseconds() +
		(uint64_t)RESCAN_INTERVAL_MS * 1000000ULL;
	while (!stopping) {
		uint64_t loop_now_ns = monotonic_nanoseconds();
		metrics_finish(bridge, &state, loop_now_ns);
		if (rescan_requested) {
			reconcile_removed_devices(&state, bridge);
			if (scan_and_grab(&state, input_root) < 0)
				goto out;
			rescan_requested = false;
			loop_now_ns = monotonic_nanoseconds();
			next_rescan_ns = loop_now_ns +
				(uint64_t)RESCAN_INTERVAL_MS * 1000000ULL;
		} else if (loop_now_ns >= next_rescan_ns) {
			reconcile_removed_devices(&state, bridge);
			loop_now_ns = monotonic_nanoseconds();
			next_rescan_ns = loop_now_ns +
				(uint64_t)RESCAN_INTERVAL_MS * 1000000ULL;
		}

		struct pollfd poll_fds[MAX_GRABBED_DEVICES + 2] = {0};
		poll_fds[0].fd = inotify_fd;
		poll_fds[0].events = POLLIN;
		for (size_t index = 0; index < state.count; index++) {
			poll_fds[index + 1].fd = state.devices[index].fd;
			poll_fds[index + 1].events = POLLIN;
		}
		nfds_t poll_count = state.count + 1;
		nfds_t display_position = 0;
		if (bridge != NULL) {
			display_position = poll_count++;
			poll_fds[display_position].fd = wl_display_get_fd(bridge->display);
			poll_fds[display_position].events = POLLIN;
		}

		uint64_t rescan_remaining_ns = next_rescan_ns > loop_now_ns ?
			next_rescan_ns - loop_now_ns : 0;
		int poll_timeout_ms = (int)((rescan_remaining_ns + 999999ULL) /
			1000000ULL);
		if (bridge != NULL && bridge->metrics_active) {
			uint64_t remaining_ns = bridge->metrics_end_ns > loop_now_ns ?
				bridge->metrics_end_ns - loop_now_ns : 0;
			uint64_t remaining_ms = (remaining_ns + 999999ULL) / 1000000ULL;
			if (remaining_ms < (uint64_t)poll_timeout_ms)
				poll_timeout_ms = (int)remaining_ms;
		}
		int poll_result = poll(poll_fds, poll_count, poll_timeout_ms);
		if (poll_result < 0 && errno != EINTR) {
			fprintf(stderr, "omarchy-input-bridge: ERROR: poll failed: %s\n",
				strerror(errno));
			goto out;
		}
		if (poll_result <= 0)
			continue;
		if ((poll_fds[0].revents & POLLIN) != 0) {
			char notifications[4096];
			while (read(inotify_fd, notifications, sizeof(notifications)) > 0)
				;
			rescan_requested = true;
		}
		if (bridge != NULL) {
			short display_events = poll_fds[display_position].revents;
			if ((display_events & (POLLHUP | POLLERR | POLLNVAL)) != 0 ||
				((display_events & POLLIN) != 0 &&
				 wl_display_dispatch(bridge->display) < 0)) {
				log_error("Wayland display connection failed");
				goto out;
			}
		}
		for (size_t position = state.count; position > 0; position--) {
			size_t index = position - 1;
			short revents = poll_fds[index + 1].revents;
			if ((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
				release_device(&state, index, bridge);
				rescan_requested = true;
			} else if ((revents & POLLIN) != 0) {
				if (drain_device(&state.devices[index], bridge) < 0) {
					fprintf(stderr,
						"omarchy-input-bridge: ERROR: input read failed for %s: %s\n",
						state.devices[index].path, strerror(errno));
					goto out;
				}
			}
		}
	}
	result = 0;

out:
	if (readiness_published && unlink(ready_file) < 0 && errno != ENOENT)
		fprintf(stderr,
			"omarchy-input-bridge: warning: cannot remove readiness at %s: %s\n",
			ready_file, strerror(errno));
	while (state.count > 0)
		release_device(&state, state.count - 1, bridge);
	if (inotify_fd >= 0)
		close(inotify_fd);
	return result;
}

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	struct bridge *bridge = data;

	if (strcmp(interface, wl_seat_interface.name) == 0 && bridge->seat == NULL) {
		bridge->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			version < 7 ? version : 7);
	} else if (strcmp(interface,
			zwp_virtual_keyboard_manager_v1_interface.name) == 0 &&
			bridge->keyboard_manager == NULL) {
		bridge->keyboard_manager = wl_registry_bind(registry, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(interface,
			zwlr_virtual_pointer_manager_v1_interface.name) == 0 &&
			bridge->pointer_manager == NULL) {
		bridge->pointer_manager = wl_registry_bind(registry, name,
			&zwlr_virtual_pointer_manager_v1_interface,
			version < 2 ? version : 2);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static int write_all(int fd, const char *buffer, size_t size)
{
	while (size > 0) {
		ssize_t written = write(fd, buffer, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buffer += written;
		size -= (size_t)written;
	}
	return 0;
}

static int publish_keymap(struct bridge *bridge, const char *layout)
{
	struct xkb_context *context = NULL;
	struct xkb_keymap *keymap = NULL;
	char *keymap_text = NULL;
	int keymap_fd = -1;
	int result = -1;
	const struct xkb_rule_names names = {
		.layout = layout,
	};

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		log_error("could not create an xkb context");
		goto out;
	}

	keymap = xkb_keymap_new_from_names(context, &names,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: could not compile XKB layout '%s'\n",
			layout);
		goto out;
	}

	keymap_text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	if (keymap_text == NULL) {
		log_error("could not serialize the XKB keymap");
		goto out;
	}

	size_t keymap_size = strlen(keymap_text) + 1;
	keymap_fd = memfd_create("omarchy-input-keymap", MFD_CLOEXEC);
	if (keymap_fd < 0 || ftruncate(keymap_fd, (off_t)keymap_size) < 0 ||
		write_all(keymap_fd, keymap_text, keymap_size) < 0) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: could not prepare XKB keymap: %s\n",
			strerror(errno));
		goto out;
	}

	zwp_virtual_keyboard_v1_keymap(bridge->keyboard,
		WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymap_fd, (uint32_t)keymap_size);
	result = 0;

out:
	if (keymap_fd >= 0)
		close(keymap_fd);
	free(keymap_text);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	return result;
}

static void bridge_destroy(struct bridge *bridge)
{
	if (bridge->pointer != NULL)
		zwlr_virtual_pointer_v1_destroy(bridge->pointer);
	if (bridge->keyboard != NULL)
		zwp_virtual_keyboard_v1_destroy(bridge->keyboard);
	if (bridge->pointer_manager != NULL)
		zwlr_virtual_pointer_manager_v1_destroy(bridge->pointer_manager);
	if (bridge->keyboard_manager != NULL)
		zwp_virtual_keyboard_manager_v1_destroy(bridge->keyboard_manager);
	if (bridge->seat != NULL)
		wl_seat_destroy(bridge->seat);
	if (bridge->registry != NULL)
		wl_registry_destroy(bridge->registry);
	if (bridge->display != NULL)
		wl_display_disconnect(bridge->display);
}

static int bridge_connect(struct bridge *bridge, const char *layout)
{
	bridge->display = wl_display_connect(NULL);
	if (bridge->display == NULL) {
		fprintf(stderr,
			"omarchy-input-bridge: ERROR: could not connect to Wayland display '%s'\n",
			getenv("WAYLAND_DISPLAY") != NULL ? getenv("WAYLAND_DISPLAY") : "(unset)");
		return -1;
	}

	bridge->registry = wl_display_get_registry(bridge->display);
	wl_registry_add_listener(bridge->registry, &registry_listener, bridge);
	if (wl_display_roundtrip(bridge->display) < 0) {
		log_error("Wayland registry roundtrip failed");
		return -1;
	}

	if (bridge->seat == NULL)
		log_error("required Wayland global wl_seat is missing");
	if (bridge->keyboard_manager == NULL)
		log_error("required Wayland global zwp_virtual_keyboard_manager_v1 is missing");
	if (bridge->pointer_manager == NULL)
		log_error("required Wayland global zwlr_virtual_pointer_manager_v1 is missing");
	if (bridge->seat == NULL || bridge->keyboard_manager == NULL ||
		bridge->pointer_manager == NULL)
		return -1;

	bridge->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
		bridge->keyboard_manager, bridge->seat);
	bridge->pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
		bridge->pointer_manager, bridge->seat);
	if (bridge->keyboard == NULL || bridge->pointer == NULL) {
		log_error("could not create Wayland virtual input objects");
		return -1;
	}

	if (publish_keymap(bridge, layout) < 0)
		return -1;
	if (wl_display_roundtrip(bridge->display) < 0 ||
		wl_display_get_error(bridge->display) != 0) {
		log_error("compositor rejected the virtual input objects");
		return -1;
	}
	return 0;
}

static int protocol_preflight(const char *layout)
{
	struct bridge bridge = {0};
	int result = 1;

	if (bridge_connect(&bridge, layout) < 0)
		goto out;

	printf("Wayland input protocol preflight passed\n");
	printf("  display: %s\n",
		getenv("WAYLAND_DISPLAY") != NULL ? getenv("WAYLAND_DISPLAY") : "(default)");
	printf("  wl_seat: found\n");
	printf("  zwp_virtual_keyboard_manager_v1: found\n");
	printf("  zwlr_virtual_pointer_manager_v1: found\n");
	printf("  XKB layout: %s\n", layout);
	result = 0;

out:
	bridge_destroy(&bridge);
	return result;
}

static int forward_events(const char *input_root, const char *seat,
		const char *layout, const char *ready_file, bool metrics_once)
{
	struct bridge bridge = {
		.metrics_enabled = metrics_once,
	};
	int result = 1;

	if (bridge_connect(&bridge, layout) < 0)
		goto out;
	result = monitor_devices(input_root, seat, &bridge, ready_file);

out:
	bridge_destroy(&bridge);
	return result;
}

static void usage(FILE *stream, const char *program)
{
	fprintf(stream,
		"Usage: %s MODE [OPTIONS]\n"
		"\n"
		"Modes:\n"
		"  --protocol-preflight  Validate Wayland virtual input protocols\n"
		"  --discover-once       Classify /host/input/event* without grabbing\n"
		"  --grab-only           Grab allowlisted devices without forwarding\n"
		"  --forward             Grab and forward devices to Wayland\n"
		"\n"
		"Options:\n"
		"  --layout LAYOUT       XKB layout for protocol preflight (default: us)\n"
		"  --input-root PATH     Evdev directory (default: /host/input)\n"
		"  --seat NAME           Sunshine name suffix (default: seat9)\n"
		"  --ready-file PATH     Publish readiness while forwarding\n"
		"  --metrics-once        Capture 10 seconds after first pointer motion\n",
		program);
}

int main(int argc, char **argv)
{
	const char *layout = getenv("OMARCHY_INPUT_LAYOUT");
	const char *input_root = INPUT_ROOT;
	const char *seat = getenv("SUNSHINE_SEAT");
	const char *ready_file = NULL;
	bool preflight = false;
	bool discovery = false;
	bool grabbing = false;
	bool forwarding = false;
	bool metrics_once = false;

	if (layout == NULL || layout[0] == '\0')
		layout = "us";
	if (seat == NULL || seat[0] == '\0')
		seat = "seat9";

	for (int index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--protocol-preflight") == 0) {
			preflight = true;
		} else if (strcmp(argv[index], "--discover-once") == 0) {
			discovery = true;
		} else if (strcmp(argv[index], "--grab-only") == 0) {
			grabbing = true;
		} else if (strcmp(argv[index], "--forward") == 0) {
			forwarding = true;
		} else if (strcmp(argv[index], "--layout") == 0 && index + 1 < argc) {
			layout = argv[++index];
		} else if (strcmp(argv[index], "--input-root") == 0 && index + 1 < argc) {
			input_root = argv[++index];
		} else if (strcmp(argv[index], "--seat") == 0 && index + 1 < argc) {
			seat = argv[++index];
		} else if (strcmp(argv[index], "--ready-file") == 0 &&
			index + 1 < argc) {
			ready_file = argv[++index];
		} else if (strcmp(argv[index], "--metrics-once") == 0) {
			metrics_once = true;
		} else if (strcmp(argv[index], "--help") == 0) {
			usage(stdout, argv[0]);
			return 0;
		} else {
			usage(stderr, argv[0]);
			return 2;
		}
	}

	if ((int)preflight + (int)discovery + (int)grabbing +
		(int)forwarding != 1) {
		usage(stderr, argv[0]);
		return 2;
	}
	if (ready_file != NULL && !forwarding) {
		usage(stderr, argv[0]);
		return 2;
	}
	if (metrics_once && !forwarding) {
		usage(stderr, argv[0]);
		return 2;
	}

	if (preflight)
		return protocol_preflight(layout);
	if (discovery)
		return discover_once(input_root, seat);
	if (grabbing)
		return monitor_devices(input_root, seat, NULL, NULL);
	return forward_events(input_root, seat, layout, ready_file, metrics_once);
}
