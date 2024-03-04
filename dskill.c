#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <CoreServices/CoreServices.h>

#define EVIL ".DS_Store"

bool is_evil(char *path) {
	return strcmp(path + strlen(path) - strlen(EVIL), EVIL) == 0;
}

void help() {
	printf("Fuck .DS_Store\n");
	printf("\n");
	printf("USAGE\n");
	printf("\n");
	printf("  $ dskill <cmd>\n");
	printf("\n");
	printf("CMD\n");
	printf("\n");
	printf("  kill      delete all .DS_Store now\n");
	printf("  guard     start daemon and prevent creation of .DS_Store forever\n");
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

int guard() {

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
			| kFSEventStreamCreateFlagNoDefer
	);

	CFRelease(paths);

	if (stream == NULL) {
		printf("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	dispatch_queue_t queue = dispatch_queue_create("dskill_queue", NULL);
	FSEventStreamSetDispatchQueue(stream, queue);

	if (!FSEventStreamStart(stream)) {
		printf("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	for (;;) {}
	dispatch_release(queue);
	FSEventStreamRelease(stream);

	return EXIT_SUCCESS;

}

char *join_path(char *p1, char *p2) {
	if (strcmp(p1, ".") == 0) {
		return strdup(p2);
	}
	int l1 = strlen(p1);
	int l2 = strlen(p2);
	char *path = malloc(l1 + l2 + 1 + 1);
	strcpy(path, p1);
	strcpy(path + strlen(p1), "/");
	strcpy(path + strlen(p1) + 1, p2);
	return path;
}

void clean_dir(char *dir) {
	DIR *d = opendir(dir);
	if (!d) {
		printf("failed to open %s\n", dir);
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(d))) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strcmp(entry->d_name, EVIL) == 0) {
			char *path = join_path(dir, EVIL);
			printf("%s\n", path);
			remove(path);
			free(path);
		} else if (entry->d_type == DT_DIR) {
			char *path = join_path(dir, entry->d_name);
			clean_dir(path);
			free(path);
		}
	}
	closedir(d);
}

int clean() {
	clean_dir(".");
	return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		help();
		return EXIT_SUCCESS;
	}
	char *cmd = argv[1];
	if (strcmp(cmd, "guard") == 0) {
		return guard();
	} else if (strcmp(cmd, "kill") == 0) {
		return clean();
	} else {
		help();
	}
	return EXIT_SUCCESS;
}
