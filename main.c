#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <termios.h>
#include <dirent.h>

#include <ctype.h>
#include <libgen.h>

#include "chip8.h"

#define	KEYBOARD_MONITOR_INPUT_FIFO_NAME "KeyboardMonitorInputFifo"
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#ifndef OMAPFB_WAITFORVSYNC_FRAME
#define OMAPFB_WAITFORVSYNC_FRAME _IOWR('O', 70, unsigned int)
#endif

char romDirectory[256] = "";
int SCREEN_SCALE = 10;
int cpf = 1;

int curPage = 0;
int fbfd = 0;
struct fb_var_screeninfo vinfo, origVinfo;
struct fb_fix_screeninfo finfo;
long int screensize = 0;
char* fbp = 0;
int pageSize = 0;

char* kbfds = "/dev/tty";
int kbfd = 0;

int quit = 0;

void fill_rect(int x, int y, int w, int h, int color, unsigned int lineLength) {
	for(int i = 0; i < h; i++) {
		unsigned int py = y + i + 80;
		/*for(int j = 0; j < w; j++) {
			unsigned int px = x + j;
			unsigned int py = y + i;
			unsigned int index = (py * finfo.line_length + px);
			index += curPage * pageSize;
			index += 80 * finfo.line_length;
			fbp[index] = color;
		}*/
		memset(fbp + (py * lineLength + x + curPage * pageSize), color, w);
	}
}

void render_screen() {
	int i = 0;
	int j = 0;
	unsigned int lineLength = finfo.line_length;
	uint8_t colors[4] = { 0, 85, 170, 15 };
	uint8_t activeColor = colors[0];
	memset(fbp + curPage * pageSize, 0, pageSize);
	memset((fbp + curPage * pageSize) + 80 * lineLength, 15, lineLength);
	memset((fbp + curPage * pageSize) + (80 + SCREEN_SCALE*32) * lineLength, 15, lineLength);
	int ss = (SCREEN_SCALE / (extendedScreen+1));
	for(j = 0; j < 1; j++) {
		if(j == 0) activeColor = colors[3];
		if(j == 1) activeColor = colors[1];
		for(i = 0; i < pixel_number; i++) {
			if(canvas_data[j][i]) {
				if(j == 1 && canvas_data[j-1][i])
					activeColor = colors[2];
				int x = (i % screen_width) * ss;
				int y = floor(i / screen_width) * ss;
				fill_rect(x, y, ss, ss, activeColor, lineLength);
			}
		}
	}
}

char* sanitizeCpf(char* input) {
	char* output;
	output = (char*)malloc(sizeof(char));
	uint8_t length = strlen(input);
	strcpy(output, "");
	for(int i = 0; i < length; i++) {
		if(isdigit(input[i])) {
			uint8_t len = strlen(output);
			char buffer[len + 2];
			strcpy(buffer, output);
			buffer[len] = input[i];
			buffer[len + 1] = '\0';
			strcpy(output, buffer);
		}
	}
	return output;
}

int inputFs = -1;
void keyboardMonitor() {
	int result;

	printf("Making Keyboard FIFO.\n");
	result = mkfifo(KEYBOARD_MONITOR_INPUT_FIFO_NAME, 0777);
	if(result == 0) {
		printf("New FIFO created.\n");
	}
	inputFs = open(KEYBOARD_MONITOR_INPUT_FIFO_NAME, (O_RDONLY | O_NONBLOCK));
	int pid2 = fork();
	if(pid2 == 0) {
		printf("Keyboard monitor thread started.\n");

		int infd;
		int inrd;
		struct input_event inputEvent[64];
		int version;
		unsigned short id[4];
		unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
		int noError = 1;

		if((infd = open("/dev/input/by-id/usb-Teensyduino_Serial_Keyboard_Mouse_Joystick_7540490-if02-event-kbd", O_RDONLY)) < 0) {
			printf("Error opening input device.");
			close(infd);
		}
		if(ioctl(infd, EVIOCGVERSION, &version)) {
			printf("Error getting input version.");
			close(infd);
		}
		if(noError) {
			ioctl(infd, EVIOCGID, id);
			memset(bit, 0, sizeof(bit));
			ioctl(infd, EVIOCGBIT(0, EV_MAX), bit[0]);
		}

		printf("Keyboard monitor child thread started.\n");

		int outputFs = -1;
		outputFs = open(KEYBOARD_MONITOR_INPUT_FIFO_NAME, (O_WRONLY | O_NONBLOCK));
		if(outputFs != -1)
			printf("Opened output FIFO.\n");

		while(noError) {
			char buffer[256];

			inrd = read(infd, inputEvent, sizeof(struct input_event) * 64);

			if(inrd < (int)sizeof(struct input_event)) {
				close(infd);
			} else {
				for(int k = 0; k < inrd / sizeof(struct input_event); k++) {
					if(inputEvent[k].type == EV_KEY) {
						memset(buffer, 0, 256);
						if(inputEvent[k].value == 2) {
							sprintf(buffer, "%d 2", inputEvent[k].code);
						} else if(inputEvent[k].value == 1) {
							sprintf(buffer, "%d 1", inputEvent[k].code);
						} else if(inputEvent[k].value == 0) {
							sprintf(buffer, "%d 0", inputEvent[k].code);
							if(inputEvent[k].code == KEY_M) noError = 0;
						}

						write(outputFs, (void*)buffer, 256);
					}
				}
			}
		}
		printf("\033[2K\r");
		close(infd);
		printf("Keyboard monitor closed.\n");
		exit(0);
	}
}

int keyReg = 0;
bool ofMode = 0;
bool cpfMode = 0;
void* keyboardThread() {
	while(!quit) {
		unsigned char inputBuffer[256];
		int bufferLength = read(inputFs, (void*)inputBuffer, 255);
		if(bufferLength < 0) {

		} else if(bufferLength == 0) {

		} else {
			inputBuffer[bufferLength] = '\0';

			char* pEnd;
			long int code = strtol(inputBuffer, &pEnd, 10);
			long int mode = strtol(pEnd, NULL, 10);

			if(mode == 1 || mode == 2)
				keyReg = code;

			if(mode < 2) {
				switch(code) {
				case KEY_1:
					keys[0x1] = mode;
					break;
				case KEY_2:
					keys[0x2] = mode;
					break;
				case KEY_3:
					keys[0x3] = mode;
					break;
				case KEY_4:
					keys[0xc] = mode;
					break;
				case KEY_Q:
					keys[0x4] = mode;
					break;
				case KEY_W:
					keys[0x5] = mode;
					break;
				case KEY_E:
					keys[0x6] = mode;
					break;
				case KEY_R:
					keys[0xd] = mode;
					break;
				case KEY_A:
					keys[0x7] = mode;
					break;
				case KEY_S:
					keys[0x8] = mode;
					break;
				case KEY_D:
					keys[0x9] = mode;
					break;
				case 33:
					keys[0xe] = mode;
					break;
				case KEY_Z:
					keys[0xa] = mode;
					break;
				case KEY_X:
					keys[0x0] = mode;
					break;
				case KEY_C:
					keys[0xb] = mode;
					break;
				case KEY_V:
					keys[0xf] = mode;
					break;
				case KEY_M:
					if(mode == 1) quit = 1;
					break;
				case KEY_L:
					if(mode == 1) {
						ofMode = 1;
					}
					break;
				case KEY_P:
					if(mode == 1) {
						cpfMode = 1;
					}
					break;
				default:
					break;
				}
			}
		}
	}
	pthread_exit(NULL);
}

void drawFs(char* romPath, int start, int maxEntries) {
	int i = 0;
	DIR* dir;
	struct dirent* ent;
	printf("\e[1;1H\e[2J");
	if((dir = opendir(romPath)) != NULL) {
		while((ent = readdir(dir)) != NULL && i <= maxEntries + start) {
			if(i >= start) {
				if(i == start)
					printf("\n  > %s\n", ent->d_name);
				else
					printf("    %s\n", ent->d_name);
			}
			i++;
		}
		closedir(dir);
	} else {
		printf("Could not open rom directory.\n");
	}
}

void chooseGame() {
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &origVinfo)) {
		printf("Error setting variable screen info.\n");
	}
	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_TEXT);
	}
	/*printf("\n\n\n\n\nOpen ROM: ");
	int c;
	while((c = getchar()) != '\n' && c != EOF && c != 'l') {}
	char input[256];
	fgets(input, 256, stdin);
	printf(input);
	ofMode = 0;
	if(!loadProgram(input)) {
		printf("Error reading file path.\n");
	}*/

	const int maxEntries = 20;
	char romPath[512];
	sprintf(romPath, "%s", "rom");
	int cursor = 0;
	int entryCount = 0;
	DIR* dir;
	struct dirent* ent;

	while(ofMode) {
		keyReg = 0;
		if((dir = opendir(romPath)) != NULL) {
			while((ent = readdir(dir)) != NULL) {
				entryCount++;
			}
			closedir(dir);
		} else {
			printf("Could not open rom directory.\n");
		}
		drawFs(romPath, cursor, maxEntries);
		while(keyReg != KEY_O) {
			if(keyReg == KEY_L) {
				cursor++;
				if(cursor > entryCount - 1) cursor = 0;
				drawFs(romPath, cursor, maxEntries);
				keyReg = 0;
			} else if(keyReg == KEY_P) {
				cursor--;
				if(cursor < 0) cursor = entryCount - 1;
				drawFs(romPath, cursor, maxEntries);
				keyReg = 0;
			} else if(keyReg == KEY_O) {
				if((dir = opendir(romPath)) != NULL) {
					int i = 0;
					while((ent = readdir(dir)) != NULL) {
						if(i == cursor) {
							struct stat pathStat;
							sprintf(romPath, "%s/%s", romPath, ent->d_name);
							stat(romPath, &pathStat);
							if(S_ISREG(pathStat.st_mode)) {
								loadProgram(romPath);
								ofMode = 0;
							} else if(S_ISDIR(pathStat.st_mode)) {

							}
						}
						i++;
					}
					closedir(dir);
				} else {
					printf("Could not open rom directory.\n");
				}
				cursor = 0;
				
			}
		}
	}

	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
	}
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
		printf("Error setting variable screen info.\n");
	}
}

void setCpf() {
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &origVinfo)) {
		printf("Error setting variable screen info.\n");
	}
	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_TEXT);
	}
	printf("\e[1;1H\e[2J");

	printf("\n  Set cycles per frame: ");
	int c;
	while((c = getchar()) != '\n' && c != EOF && c != 'p') {}
	char input[8];
	fgets(input, 8, stdin);
	printf(input);
	cpfMode = 0;
	cpf = atoi(input);

	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
	}
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
		printf("Error setting variable screen info.\n");
	}
}

int main(int argc, char* argv[]) {
	keyboardMonitor();

	bool nogui = false;
	char openFile[256] = "null";
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("\nUsage:\n\n\t-h, --help\t\tDisplay available command line arguments.\n");
			printf("\t-n, --nogui\t\tStart Chip-84 without UI elements, leaving only the display shown.\n");
			printf("\t-o, --open [path]\tDirectly load a ROM upon launch.\n");
			printf("\t-c, --cpf [number]\tStart Chip-84 with an initial cycles per frame value.\n");
			printf("\t-s, --screenscale [number]\tSet the scale of the display.\n");
			return 1;
		}
		if(strcmp(argv[i], "--nogui") == 0 || strcmp(argv[i], "-n") == 0) {
			nogui = true;
		}
		if(strcmp(argv[i], "--open") == 0 || strcmp(argv[i], "-o") == 0) {
			if(i+1<argc)
				strcpy(openFile, argv[i+1]);
		}
		if(strcmp(argv[i], "--cpf") == 0 || strcmp(argv[i], "-c") == 0) {
			if(i+1<argc)
				cpf = atoi(sanitizeCpf(argv[i+1]));
		}
		if(strcmp(argv[i], "--screenscale") == 0 || strcmp(argv[i], "-s") == 0) {
			if(i+1<argc)
				SCREEN_SCALE = atoi(sanitizeCpf(argv[i+1]));
		}
	}

	fbfd = open("/dev/fb0", O_RDWR);
	if(!fbfd) {
        printf("Error: cannot open framebuffer device.\n");
        return 1;
    }
    printf("Framebuffer device opened.\n");

    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        printf("Error reading variable screen info.\n");
    }
    printf("Display info: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    memcpy(&origVinfo, &vinfo, sizeof(struct fb_var_screeninfo));

    vinfo.bits_per_pixel = 8;
    vinfo.xres = 640;
    vinfo.yres = 480;
    vinfo.xres_virtual = vinfo.xres;
	vinfo.yres_virtual = vinfo.yres * 2;
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
        printf("Error setting variable screen info.\n");
    }

	kbfd = open(kbfds, O_WRONLY);
	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
	} else {
		printf("Could not open %s.\n", kbfds);
	}

    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed screen info.\n");
    }

    pageSize = finfo.line_length * vinfo.yres;
    screensize = finfo.smem_len;

    fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if(!fbp) {
    	printf("Failed to mmap.\n");
    } else {
    	if(strcmp(openFile, "null") != 0) {
			loadProgram(openFile);
		}

		pthread_t ptid;
		pthread_create(&ptid, NULL, &keyboardThread, NULL);

		bool ctrl = false;

		if(inputFs != -1) {
			while(!quit) {
				emulateCycle(cpf);
				render_screen();

				unsigned int dummy = 0;
				ioctl(fbfd, FBIO_WAITFORVSYNC, &dummy);

				curPage = (curPage + 1) % 2;
				vinfo.yoffset = curPage * vinfo.yres;
				vinfo.activate = FB_ACTIVATE_VBL;
				ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo);

				if(ofMode) {
					chooseGame();
				}
				if(cpfMode) {
					setCpf();
				}
			}
		}
    }



	
	munmap(fbp, screensize);
	if(kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_TEXT);
		close(kbfd);
	}
	if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &origVinfo)) {
        printf("Error setting variable screen info.\n");
    }
    close(fbfd);

	exit(0);
	
	return 0;
}
