#include "gif_lib.h"

#include <stdio.h>
#include <string.h>

typedef struct MemorySource {
	const GifByteType *data;
	size_t size;
	size_t offset;
} MemorySource;

static int failures;

#ifdef GIFLIB_TEST_ALLOC_TRACKING
size_t giflib_test_outstanding_allocations(void);
#endif

#define CHECK(condition)                                                       \
	do {                                                                     \
		if (!(condition)) {                                                \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,     \
			        __LINE__, #condition);                               \
			failures++;                                                 \
		}                                                                \
	} while (0)

static int memory_read(GifFileType *gif, GifByteType *destination, int length) {
	MemorySource *source = (MemorySource *)gif->UserData;
	size_t available;
	size_t requested;

	if (source == NULL || destination == NULL || length <= 0) {
		return 0;
	}

	available = source->size - source->offset;
	requested = (size_t)length;
	if (requested > available) {
		requested = available;
	}

	if (requested != 0) {
		memcpy(destination, source->data + source->offset, requested);
		source->offset += requested;
	}

	return (int)requested;
}

static GifFileType *open_memory(MemorySource *source,
	                            const GifByteType *data,
	                            size_t size,
	                            int *error) {
	source->data = data;
	source->size = size;
	source->offset = 0;
	return DGifOpen(source, memory_read, error);
}

static void test_decode_one_pixel(void) {
	static const GifByteType gif_data[] = {
	    'G', 'I', 'F', '8', '9', 'a',
	    0x01, 0x00, 0x01, 0x00,
	    0x80, 0x00, 0x00,
	    0x00, 0x00, 0x00,
	    0xff, 0xff, 0xff,
	    0x2c,
	    0x00, 0x00, 0x00, 0x00,
	    0x01, 0x00, 0x01, 0x00,
	    0x00,
	    0x02,
	    0x02, 0x44, 0x01,
	    0x00,
	    0x3b,
	};
	MemorySource source;
	GifFileType *gif;
	GifRecordType record;
	GifPixelType pixel = 0xff;
	int error = -1;

	gif = open_memory(&source, gif_data, sizeof(gif_data), &error);
	CHECK(gif != NULL);
	if (gif == NULL) {
		return;
	}

	CHECK(gif->SWidth == 1);
	CHECK(gif->SHeight == 1);
	CHECK(gif->SColorMap != NULL);
	CHECK(gif->SColorMap != NULL && gif->SColorMap->ColorCount == 2);

	CHECK(DGifGetRecordType(gif, &record) == GIF_OK);
	CHECK(record == IMAGE_DESC_RECORD_TYPE);
	CHECK(DGifGetImageHeader(gif) == GIF_OK);
	CHECK(gif->ImageCount == 0);
	CHECK(gif->SavedImages == NULL);
	CHECK(DGifGetLine(gif, &pixel, 1) == GIF_OK);
	CHECK(pixel == 0);

	CHECK(DGifGetRecordType(gif, &record) == GIF_OK);
	CHECK(record == TERMINATE_RECORD_TYPE);
	CHECK(DGifCloseFile(gif, &error) == GIF_OK);
	CHECK(error == D_GIF_SUCCEEDED);
}

static void test_bad_signature(void) {
	static const GifByteType not_gif[] = {
	    'N', 'O', 'T', '8', '9', 'a',
	};
	MemorySource source;
	GifFileType *gif;
	int error = -1;

	gif = open_memory(&source, not_gif, sizeof(not_gif), &error);
	CHECK(gif == NULL);
	CHECK(error == D_GIF_ERR_NOT_GIF_FILE);
}

static void test_screen_descriptor_error_is_preserved(void) {
	static const GifByteType truncated[] = {
	    'G', 'I', 'F', '8', '9', 'a',
	    0x01,
	};
	MemorySource source;
	GifFileType *gif;
	int error = -1;

	gif = open_memory(&source, truncated, sizeof(truncated), &error);
	CHECK(gif == NULL);
	CHECK(error == D_GIF_ERR_READ_FAILED);
}

static void test_maximum_image_dimensions_header(void) {
	static const GifByteType gif_data[] = {
	    'G', 'I', 'F', '8', '9', 'a',
	    0x01, 0x00, 0x01, 0x00,
	    0x00, 0x00, 0x00,
	    0x2c,
	    0x00, 0x00, 0x00, 0x00,
	    0xff, 0xff, 0xff, 0xff,
	    0x00,
	    0x02,
	};
	MemorySource source;
	GifFileType *gif;
	GifRecordType record;
	int error = -1;

	gif = open_memory(&source, gif_data, sizeof(gif_data), &error);
	CHECK(gif != NULL);
	if (gif == NULL) {
		return;
	}

	CHECK(DGifGetRecordType(gif, &record) == GIF_OK);
	CHECK(record == IMAGE_DESC_RECORD_TYPE);
	CHECK(DGifGetImageHeader(gif) == GIF_OK);
	CHECK(gif->Image.Width == 65535);
	CHECK(gif->Image.Height == 65535);
	CHECK(DGifCloseFile(gif, &error) == GIF_OK);
}

static void test_repeated_open_decode_close(void) {
	int iteration;
#ifdef GIFLIB_TEST_ALLOC_TRACKING
	size_t allocations_before = giflib_test_outstanding_allocations();
#endif

	for (iteration = 0; iteration < 1000; iteration++) {
		test_decode_one_pixel();
		if (failures != 0) {
			break;
		}
	}

#ifdef GIFLIB_TEST_ALLOC_TRACKING
	CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

int main(void) {
	test_decode_one_pixel();
	test_bad_signature();
	test_screen_descriptor_error_is_preserved();
	test_maximum_image_dimensions_header();
	test_repeated_open_decode_close();

	if (failures != 0) {
		fprintf(stderr, "%d regression check(s) failed\n", failures);
		return 1;
	}

	puts("giflib baseline regression tests passed");
	return 0;
}
