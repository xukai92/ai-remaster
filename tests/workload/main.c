// SPDX-License-Identifier: CC0-1.0
// Synthetic, deterministic PSP texture scene for remaster integration tests.
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RemasterFixture", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define WIDTH 480
#define HEIGHT 272
#define STRIDE 512
#define DEFAULT_FRAMES 180

typedef struct {
	float u, v;
	float x, y, z;
} Vertex;

static unsigned int __attribute__((aligned(16))) display_list[262144];
static unsigned int __attribute__((aligned(16))) checker[128 * 128];
static unsigned int __attribute__((aligned(16))) gradient[64 * 64];
static unsigned int __attribute__((aligned(16))) tiny[8 * 8];
static unsigned int __attribute__((aligned(16))) changing[64 * 64];
static Vertex __attribute__((aligned(16))) quads[4][4];

static unsigned int rgba(unsigned int r, unsigned int g, unsigned int b) {
	return 0xff000000u | (b << 16) | (g << 8) | r;
}

static void make_textures(void) {
	int x, y;
	for (y = 0; y < 128; ++y) {
		for (x = 0; x < 128; ++x) {
			unsigned int c = ((x / 16) ^ (y / 16)) & 1 ? rgba(242, 202, 72) : rgba(36, 82, 154);
			if (x < 3 || x >= 125 || y < 3 || y >= 125)
				c = rgba(255, 255, 255);
			checker[y * 128 + x] = c;
		}
	}
	for (y = 0; y < 64; ++y) {
		for (x = 0; x < 64; ++x) {
			unsigned int c = rgba(35 + x * 3, 38 + y * 3, 135);
			if (x < 2 || x >= 62 || y < 2 || y >= 62)
				c = rgba(255, 255, 255);
			gradient[y * 64 + x] = c;
		}
	}
	for (y = 0; y < 8; ++y)
		for (x = 0; x < 8; ++x)
			tiny[y * 8 + x] = ((x ^ y) & 1) ? rgba(255, 70, 70) : rgba(45, 10, 10);
}

static void update_changing(int frame) {
	int x, y;
	for (y = 0; y < 64; ++y) {
		for (x = 0; x < 64; ++x) {
			unsigned int v = (unsigned int)((x * 3 + y * 5 + frame * 11) & 255);
			changing[y * 64 + x] = rgba(v, 255 - v, (v + 97) & 255);
		}
	}
	sceKernelDcacheWritebackRange(changing, sizeof(changing));
}

static void make_quad(int index, float center_x) {
	const float left = center_x - 0.74f;
	const float right = center_x + 0.74f;
	quads[index][0] = (Vertex){0.0f, 0.0f, left, 0.90f, -0.14f};
	quads[index][1] = (Vertex){1.0f, 0.0f, right, 0.90f, 0.14f};
	quads[index][2] = (Vertex){1.0f, 1.0f, right, -0.90f, 0.14f};
	quads[index][3] = (Vertex){0.0f, 1.0f, left, -0.90f, -0.14f};
}

static void draw_texture(int index, int size, const unsigned int *pixels) {
	sceGuTexImage(0, size, size, size, pixels);
	sceGuTexFlush();
	sceGumDrawArray(GU_TRIANGLE_FAN,
		GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
		4, NULL, quads[index]);
}

static int frame_count(int argc, char **argv) {
	int n;
	if (argc != 3 || argv[1] == NULL || argv[2] == NULL ||
		strcmp(argv[1], "--frames") != 0)
		return DEFAULT_FRAMES;
	n = atoi(argv[2]);
	return n >= 1 && n <= 3600 ? n : DEFAULT_FRAMES;
}

int main(int argc, char **argv) {
	int frame, frames = frame_count(argc, argv);
	void *fb;
	make_textures();
	make_quad(0, -2.55f);
	make_quad(1, -0.85f);
	make_quad(2, 0.85f);
	make_quad(3, 2.55f);
	sceKernelDcacheWritebackRange(checker, sizeof(checker));
	sceKernelDcacheWritebackRange(gradient, sizeof(gradient));
	sceKernelDcacheWritebackRange(tiny, sizeof(tiny));
	sceKernelDcacheWritebackRange(quads, sizeof(quads));

	sceGuInit();
	sceGuStart(GU_DIRECT, display_list);
	sceGuDrawBuffer(GU_PSM_8888, (void *)0, STRIDE);
	sceGuDispBuffer(WIDTH, HEIGHT, (void *)(STRIDE * HEIGHT * 4), STRIDE);
	sceGuDepthBuffer((void *)(STRIDE * HEIGHT * 8), STRIDE);
	sceGuOffset(2048 - WIDTH / 2, 2048 - HEIGHT / 2);
	sceGuViewport(2048, 2048, WIDTH, HEIGHT);
	sceGuDepthRange(65535, 0);
	sceGuScissor(0, 0, WIDTH, HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuDisable(GU_CULL_FACE);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	printf("REM_FIXTURE start frames=%d\n", frames);

	for (frame = 0; frame < frames; ++frame) {
		ScePspFVector3 camera = {0.0f, 0.0f, -4.0f};
		update_changing(frame);
		sceGuStart(GU_DIRECT, display_list);
		sceGuClearColor(rgba(18, 22, 30));
		sceGuClear(GU_COLOR_BUFFER_BIT);
		sceGumMatrixMode(GU_PROJECTION);
		sceGumLoadIdentity();
		sceGumPerspective(60.0f, (float)WIDTH / HEIGHT, 0.5f, 20.0f);
		sceGumMatrixMode(GU_VIEW);
		sceGumLoadIdentity();
		sceGumMatrixMode(GU_MODEL);
		sceGumLoadIdentity();
		sceGumTranslate(&camera);
		draw_texture(0, 128, checker);
		draw_texture(1, 64, gradient);
		draw_texture(2, 8, tiny);
		draw_texture(3, 64, changing);
		sceGuFinish();
		sceGuSync(0, 0);
		sceDisplayWaitVblankStart();
		fb = sceGuSwapBuffers();
		(void)fb;
		if (frame == (frames < 61 ? frames - 1 : 60))
			sceIoDevctl("kemulator:", 0x20, NULL, 0, NULL, 0);
	}
	printf("REM_FIXTURE done frames=%d\n", frames);
	sceGuTerm();
	sceKernelExitGame();
	return 0;
}
