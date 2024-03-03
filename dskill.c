#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <CoreServices/CoreServices.h>

#define EVIL ".DS_Store"

bool is_evil(char *path) {
	return strcmp(path + strlen(path) - strlen(EVIL), EVIL) == 0;
}

void fsevent_callback(
	ConstFSEventStreamRef ref,
	void *client_callBack_info,
	size_t num_events,
	void *event_paths,
	const FSEventStreamEventFlags *event_flags,
	const FSEventStreamEventId *event_ids
) {
	for (int i = 0; i < num_events; i++) {
		char *path = ((char**)event_paths)[i];
		if (!is_evil(path)) {
			continue;
		}
		FSEventStreamEventFlags flags = event_flags[i];
		if (flags & kFSEventStreamEventFlagItemRemoved) {
			continue;
		}
		if (flags & kFSEventStreamEventFlagItemCreated) {
			printf("removing %s\n", path);
			remove(path);
		}
	}
}

int main() {

	CFStringRef path = CFSTR(".");
	CFArrayRef paths = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

	FSEventStreamRef stream = FSEventStreamCreate(
		NULL,
		fsevent_callback,
		NULL,
		paths,
		kFSEventStreamEventIdSinceNow,
		0.5,
		kFSEventStreamCreateFlagFileEvents
	);

	CFRelease(paths);

	if (stream == NULL) {
		printf("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	dispatch_queue_t fsevents_queue = dispatch_queue_create("dskill_event_queue", NULL);
	FSEventStreamSetDispatchQueue(stream, fsevents_queue);

	if (!FSEventStreamStart(stream)) {
		printf("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	for (;;) {}
	dispatch_release(fsevents_queue);
	FSEventStreamRelease(stream);

	return 0;

}
