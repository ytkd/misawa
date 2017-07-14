#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <windows.h>
#include <mmsystem.h>

#include "misc.h"
#include "wave.h"

#define STACK_SIZE (2 * 1024 * 1024)

#define BUFFER_SIZE (16 * 1024)

int snd_playing;
int snd_accept;

extern struct setup_info setup;

static void CALLBACK waveCallback(HWAVE h, UINT msg, DWORD data, DWORD p1, DWORD p2);
static DWORD CALLBACK sound_thread(void *param);
static int make_wave(int plane);

struct wave_info{
  HWAVEOUT dev;
  HANDLE ev;
  HANDLE run;
  int open;
  int use;
  WAVEHDR wh[WAVE_PLANE];
  HANDLE thread;
  DWORD tid;
  int plane;
  int left;
  void *ptr[WAVE_PLANE];
  char *buffer;
  int pass;
  int failed;
  int max;
  int error;
};

struct channel_info{
  int hz;
  int length;
  int split;
  int step;
  int ex;
  int v;
  int on;
  int type;
  int keep;
  int duty;
};


static struct wave_info wi;

static struct wave_pcm *pcm_cur;
static struct wave_pcm *pcm_last;

static CRITICAL_SECTION csPCM;

int is_valid_buffer()
{
	DWORD *pc;
	int cur;

	pc = (DWORD *)((char *)wi.buffer + BUFFER_SIZE * WAVE_PLANE);
	cur = 4096 / 4;
	while (cur) {
		if (*pc != 0x7fabcdff) {
			return 0;
		}
		++pc;
		--cur;
	}
	return 1;
}



int sound_open(void)
{
	HWAVEOUT out;
	WAVEFORMATEX wf = {WAVE_FORMAT_PCM, WAVE_CH, WAVE_RATE, (WAVE_RATE * WAVE_CH * WAVE_BIT / 8), 4, WAVE_BIT, 0};
	int i;
	char *p;
	int n;

	wi.open = 0;
	if (waveOutGetNumDevs() == 0) {
		return 1;
	}

	InitializeCriticalSection(&csPCM);
	EnterCriticalSection(&csPCM);
	pcm_cur = NULL;
	pcm_last= NULL;
	LeaveCriticalSection(&csPCM);

	zeromem(&wi, sizeof(wi));
	wi.ev = CreateEvent(NULL, FALSE, FALSE, NULL);
	wi.run = CreateEvent(NULL, FALSE, FALSE, NULL);
	p = m_alloc(BUFFER_SIZE * WAVE_PLANE + 4096);
	m_set32(p + BUFFER_SIZE * WAVE_PLANE, 0x7fabcdff, 4096),
	wi.buffer = p;

	wi.plane = 0;
	wi.left = WAVE_PLANE;

	ResetEvent(wi.ev);
	if (waveOutOpen(&out, WAVE_MAPPER, &wf, (DWORD)waveCallback, (DWORD)&wi, CALLBACK_FUNCTION) != 0) {
		return 1;
	}
	wi.dev = out;
	zeromem(&wi.wh[0], sizeof(wi.wh));
	for(i = 0; i < WAVE_PLANE; i++) {
		wi.ptr[i] = p;
		wi.wh[i].lpData = p;
		wi.wh[i].dwBufferLength = BUFFER_SIZE;
		p += BUFFER_SIZE;
	}

	for (i = 0; i < WAVE_PLANE; i++) {
		n = waveOutPrepareHeader(wi.dev, &wi.wh[i], sizeof(WAVEHDR));
		if (n) {
			while (i) {
				--i;
				waveOutUnprepareHeader(wi.dev, &wi.wh[i], sizeof(WAVEHDR));
			}
			ResetEvent(wi.ev);
			waveOutClose(wi.dev);
			WaitForSingleObject(wi.ev, INFINITE);
			CloseHandle(wi.ev);
			CloseHandle(wi.run);
			m_free(wi.buffer);
			return 1;
		}
	}

	wi.thread = CreateThread(NULL, STACK_SIZE, sound_thread, NULL, CREATE_SUSPENDED, &wi.tid);
	if (!wi.thread) {
		i = WAVE_PLANE;
		while (i) {
			--i;
			waveOutUnprepareHeader(wi.dev, &wi.wh[i], sizeof(WAVEHDR));
		}
		wi.open = 0;
		ResetEvent(wi.ev);
		waveOutClose(wi.dev);
		WaitForSingleObject(wi.ev, INFINITE);
		CloseHandle(wi.ev);
		CloseHandle(wi.run);
		m_free(wi.buffer);
		return 1;
	}

	wi.open = 1;
	SetThreadPriority(wi.thread, THREAD_PRIORITY_ABOVE_NORMAL);
	ResumeThread(wi.thread);

	return 0;
}

void sound_close(void)
{
	int n;
	int i;

	if (wi.open == 0) {
		return;
	}

	waveOutReset(wi.dev);
	wi.open = 0;
	SetEvent(wi.run);
	WaitForSingleObject(wi.thread, INFINITE);

	CloseHandle(wi.thread);

	for (i = 0; i < WAVE_PLANE; ++i) {
		if (wi.wh[i].dwFlags & WHDR_PREPARED) {
			waveOutUnprepareHeader(wi.dev, &wi.wh[i], sizeof(wi.wh[i]));
		}
	}

	ResetEvent(wi.ev);
	n = waveOutClose(wi.dev);
	if (n == MMSYSERR_NOERROR) {
		WaitForSingleObject(wi.ev, INFINITE);
	}

	CloseHandle(wi.ev);
	CloseHandle(wi.run);
	m_free(wi.buffer);
}

static DWORD CALLBACK sound_thread(void *param)
{
	int cur;
	int ret;
	int phase;
	int add;
	void *p;

	phase = 0;
	snd_playing = 0;
	while (wi.open) {
		ret = WaitForSingleObject(wi.run, 0);
		if (ret == WAIT_TIMEOUT) {
			ret = WaitForSingleObject(wi.run, INFINITE);
			if (ret == WAIT_FAILED) {
				ret = GetLastError();
				continue;
			}
		}

		if (pcm_cur == NULL) {
			is_valid_buffer();
			snd_playing = 0;
			continue;
		}

		is_valid_buffer();

		if (!wi.open) {
			break;
		}

		add = 0;
		while (wi.left) {
			if (!wi.open) {
				goto DONE;
			}
			cur = wi.plane;
			switch (phase) {
			case 0:
				if (make_wave(cur) == 0) {
					goto WAIT;
				}
				phase++;
			case 1:
				if (!wi.open) {
					goto DONE;
				}
				snd_playing = 1;
				ret =  waveOutWrite(wi.dev, &wi.wh[cur], sizeof(WAVEHDR));
				if (ret != 0) {
					wi.failed++;
					wi.error = ret;
					goto WAIT;
				}
				phase = 0;
			}
			add++;
			wi.pass++;
			wi.plane++;
			if (WAVE_PLANE <= wi.plane) {
				wi.plane = 0;
			}
			wi.left--;
		}
	WAIT:
		if (wi.max < add) {
			wi.max = add;
		}
	}
DONE:
	while (wi.left != WAVE_PLANE) {
		ret = WaitForSingleObject(wi.run, INFINITE);
	}

	while (pcm_cur) {
		p = pcm_cur;
		pcm_cur = pcm_cur->next;
		m_free(p);
	}
	pcm_cur = NULL;
	pcm_last = NULL;

	return 0;
}

static void CALLBACK waveCallback(HWAVE h, UINT msg, DWORD data, DWORD p1, DWORD p2)
{
	switch (msg) {
	case MM_WOM_OPEN:
		SetEvent(wi.ev);
		break;

	case MM_WOM_DONE:
		wi.left++;
		SetEvent(wi.run);
		break;

	case MM_WOM_CLOSE:
		SetEvent(wi.ev);
		break;
	}
}

void pcm_set(struct pcm_data *pd)
{
	struct wave_pcm *p;
	struct wave_pcm *now = pcm_cur;
	int n;
	void *pcm;
	unsigned int len;

	if (wi.dev == NULL) {
		return;
	}
	if (pd == NULL) {
		return;
	}

	pcm = pd->data;
	len = pd->len;
	EnterCriticalSection(&csPCM);
	now = pcm_cur;

	/* ToDo: add to protect for exclusive routine.
     sound_thread may access this buffer.
	 */
	while (now != NULL && len != 0) {
		n = len;
		if (now->left < len) {
			n = now->left;
		}

		//mcopy(now->ptr, pcm, n);
		m_add(now->ptr, pcm, n);
		pcm = ((char *)pcm + n);
		len -= n;
		now = now->next;
	}
	LeaveCriticalSection(&csPCM);

	if (len == 0) {
		return;
	}

	n = len + sizeof(struct wave_pcm) - sizeof(p->data);
	p = m_alloc(n);
	if (p == NULL) {
		return;
	}

	p->left = len;
	p->next = NULL;
	p->ptr = p->data;
	mcopy(p->data, pcm, len);

	EnterCriticalSection(&csPCM);
	if (pcm_cur == NULL) {
		pcm_cur = p;
	} else if (pcm_last) {
		pcm_last->next = p;
	}
	pcm_last = p;
	LeaveCriticalSection(&csPCM);

}

void sound_run(void)
{
	if (wi.dev == NULL) {
		return;
	}
	if (pcm_cur == NULL) {
		return;
	}
	if (snd_playing) {
		snd_accept = 0;
		return;
	}

	snd_accept = 1;
	SetEvent(wi.run);
}

int last_copied;

static int make_wave(int plane)
{
	char *p = wi.ptr[plane];
	struct wave_pcm *cur = pcm_cur;
	void *f;
	int len;
	int left;

	if (cur == NULL) {
		return 0;
	}

	left = BUFFER_SIZE;

	EnterCriticalSection(&csPCM);
	cur = pcm_cur;

	while (cur != NULL && left != 0) {
		//len = left / WAVE_CH;
		len = left;
		if (cur->left < len) {
			len = cur->left;
		}

//		mcopy_2b(p, cur->ptr, len);
		mcopy(p, cur->ptr, len);
		is_valid_buffer();
		last_copied = len;

		cur->ptr = (char *)(cur->ptr) + len;
		cur->left -= len;
		p += len;

		if (cur->left == 0) {
			is_valid_buffer();
			f = cur;
			cur = cur->next;
			pcm_cur = cur;
			if (pcm_last == f) {
				pcm_last = NULL;
			}
			m_free(f);
			is_valid_buffer();
		}
//		len *= 2;
		if (left < len) {
			len = left;
		}
		left -= len;

	}
	LeaveCriticalSection(&csPCM);

	is_valid_buffer();
	if (left) {
		zeromem(p, left);
	}
	is_valid_buffer();

	return 1;
}


void sound_stop(int ch)
{
	if (wi.dev == NULL) {
		return;
	}

	waveOutReset(wi.dev);
}


struct riff_header {
	DWORD tag;
	DWORD size;
	DWORD type;
};

struct chunk_header {
	DWORD tag;
	DWORD size;
};

struct pcm_data *load_waveform(const char *filename)
{
	HANDLE h;
	struct pcm_data *pcm;
	struct riff_header rh;
	struct chunk_header ch;
	WAVEFORMAT wf;
	unsigned long int red;
	int done;

	h = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	ReadFile(h, &rh, sizeof(rh), &red, NULL);
	if (rh.tag != FOURCC_RIFF) {
		return NULL;
	}

	done = 0;
	pcm = NULL;
	do {
		ReadFile(h, &ch, sizeof(ch), &red, NULL);
		switch (ch.tag) {
		case MAKEFOURCC('f', 'm', 't', ' '):
			ReadFile(h, &wf, sizeof(wf), &red, NULL);
			SetFilePointer(h, ch.size - sizeof(wf), NULL, SEEK_CUR);
			break;
		case MAKEFOURCC('d', 'a', 't', 'a'):
			pcm = m_alloc(sizeof(*pcm) + ch.size);
			pcm->len = ch.size;
			pcm->data = &pcm[1];
			ReadFile(h, pcm->data, ch.size, &red, NULL);
			done = 1;
			break;
		default:
			SetFilePointer(h, ch.size, NULL, SEEK_CUR);
			break;
		}
	} while (!done);

	CloseHandle(h);

	return pcm;
}
