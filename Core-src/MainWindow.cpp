/*
   	Copyright (c) 2003, Xentronix
	Author: Frans van Nispen (frans@xentronix.com)
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without modification,
	are permitted provided that the following conditions are met:
	
	Redistributions of source code must retain the above copyright notice, this list
	of conditions and the following disclaimer. Redistributions in binary form must
	reproduce the above copyright notice, this list of conditions and the following
	disclaimer in the documentation and/or other materials provided with the distribution. 
	
	Neither the name of Xentronix nor the names of its contributors may be used
	to endorse or promote products derived from this software without specific prior
	written permission. 
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
	SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <Application.h>
#include <InterfaceKit.h>
#include <StorageKit.h>
#include <MediaKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <Cursor.h>
#include <String.h>
#include <string.h>

#include "Globals.h"
#include "MainWindow.h"
#include "VUView.h"
#include "main.h"
#include "TransportView.h"
#include "PointersView.h"
#include "ToolBarView.h"
#include "BigView.h"
#include "SampleView.h"
#include "ValuesView.h"
#include "IndexView.h"
#include "TimeBarView.h"
#include "Filters.h"
#include "PrefWindow.h"
#include "FreqWindow.h"
#include "MyClipBoard.h"
#include "Shortcut.h"
#include "FilterDialogs.h"
#include "Analyzers.h"
#include "TempoAnalyzer.h"
#include "TunerWindow.h"
#include "MidiImport.h"
#include "VMSystem.h"
#include "AudioRecorder.h"
#include "LadspaFilter.h"
#include "Mixer.h"
#include "Tracks.h"
#include "TrackPanelView.h"
#include "LanesView.h"
#include "FileImport.h"
#include <strings.h>
#include "Project.h"

#define MIXDOWN_AS			'mixA'
#define MSG_MIXDOWN_FILE	'mixF'
#define PROJECT_NEW			'prjW'
#define MSG_PROJECT_FOLDER	'prjF'
#define MSG_PROJECT_NAME	'prjN'
#define MSG_PROJECT_FILE	'prjP'
#define MSG_CLIP_COUNT		'cCnt'
#define MSG_CLIP_FILE		'cFil'

static BFilePanel *gProjFolderPanel = NULL;
static BFilePanel *gProjOpenPanel = NULL;
static BFilePanel *gMixdownPanel = NULL;
static BFilePanel *gClipSavePanel = NULL;
static BPath	   gProjParent;

cookie_record play_cookie;
#define UPDATE	'updt'

// URL to the homepage/Tutorials/others
//char *TUTORIALS_URL = "http://www.xentronix.com/module.php?mod=document";
const char *HOMEPAGE_URL = "https://github.com/HaikuArchives/BeAE/";


class MyMenuBar : public BMenuBar{
  public:
  	MyMenuBar(BRect frame, const char *name);
	virtual void MakeFocus(bool b);
};

MyMenuBar::MyMenuBar(BRect r, const char *name) : BMenuBar(r, name)
{
}

void MyMenuBar::MakeFocus(bool b)
{
	// This one does make sure the MenuBar does NOT get focus !!!
	// To avoid the key-navigation to stop working
}


#ifdef __VM_SYSTEM

void BufferPlayer(void *theCookie, void *buffer, size_t size, const media_raw_audio_format &format)
{
	// We're going to be cheap and only work for floating-point audio 
	if (format.format != media_raw_audio_format::B_AUDIO_FLOAT) { 
		return; 
	}
	
	bool stop_needed = false;
	size_t i; 
	float *buf = (float *) buffer; 
	size_t float_size = size/4; 
	cookie_record *cookie = (cookie_record *) theCookie;
	float left = 0.0, right = 0.0;
	float fraq = fabs(Pool.frequency/Pool.system_frequency);
	
	if ((Pool.selection == NONE) | cookie->end){
		cookie->end_mem = Pool.size*Pool.sample_type;
	}else{
		cookie->end_mem = Pool.r_sel_pointer*Pool.sample_type;
	}
	
	int32 mem_size = float_size * (int32)ceil(fraq);
	float *mem = cookie->buffer;
	VM.ReadCache( mem, mem_size );
//	VM.ReadBlockAt( cookie->mem, mem, mem_size );

	// Now fill the buffer with sound! 

	if (cookie->pause){
		for (i=0; i<float_size; i++) { 
			buf[i] = 0.0;
		}
	}else{
		if (Pool.sample_type == MONO){	//cookie->mono){
			for (i=0; i<float_size; i+=2){
				if (cookie->mem >= cookie->end_mem){
					if (cookie->loop){
						cookie->mem = Pool.pointer*Pool.sample_type;
						VM.SetPlayPointer( cookie->mem );
						VM.ReadCache( mem, mem_size );
					}else{
						buf[i] = 0.0;
						buf[i+1] = 0.0;
						stop_needed = true;
					}
				}
				if (!stop_needed){
					buf[i] = *mem;
					buf[i+1] = *mem;

					cookie->add += fraq;
					if (Pool.frequency>=0){
						while (cookie->add >= 1.0){
							cookie->mem++;
							mem++;
							--cookie->add;
						}
					}else{
						while (cookie->add >= 1.0){
							cookie->mem--;
							mem--;
							--cookie->add;
						}
						if (cookie->mem < 0)
							cookie->mem += cookie->end_mem;
					}
				}
			}
		}else{
			for (i=0; i<float_size; i+=2) { 
				if (cookie->mem >= cookie->end_mem){
					if (cookie->loop){
						cookie->mem = Pool.pointer*Pool.sample_type;
						VM.SetPlayPointer( cookie->mem );
						VM.ReadCache( mem, mem_size );
					}else{
						buf[i] = 0.0f;
						buf[i+1] = 0.0f;
						stop_needed = true;
					}
				}
				if (!stop_needed){
					switch(Pool.selection){
					case NONE:
					case BOTH:
						buf[i] = mem[0];
						buf[i+1] = mem[1];
						break;
					case LEFT:
						buf[i] = mem[0];
						buf[i+1] = mem[0];
						break;
					case RIGHT:
						buf[i] = mem[1];
						buf[i+1] = mem[1];
						break;
					}

					cookie->add += fraq;
					if (Pool.frequency>=0){
						while (cookie->add >= 1.0){
							cookie->mem+= 2;
							mem+=2;
							--cookie->add;
						}
					}else{
						while (cookie->add >= 1.0){
							cookie->mem-= 2;
							mem-=2;
							--cookie->add;
						}
						if (cookie->mem < 0)
							cookie->mem += cookie->end_mem;
					}
				}
			}
		}
	}
	
	Pool.last_pointer = cookie->mem;	// set the last played location
	if (Pool.sample_type == STEREO)
		Pool.last_pointer >>= 1;

	if (stop_needed)
		Pool.mainWindow->PostMessage(TRANSPORT_STOP);

	for (int i=0; i<PLAY_HOOKS; i++){
		if (Pool.BufferHook[i]){
			(Pool.BufferHook[i])(buf, float_size, Pool.BufferCookie[i]);
		}
	}
	

	for (i=0; i<float_size; i+=2) { 
		left = MAX(buf[i], left);
		right = MAX(buf[i+1],right);
	}
	cookie->left = left;
	cookie->right = right;

	// update the visuals
	cookie->count -= size;
	if (cookie->count <0){
		cookie->count = (int)Pool.system_frequency/3;						// 24 times a second
			Pool.mainWindow->PostMessage(UPDATE);
	}
}

#else
void BufferPlayer(void *theCookie, void *buffer, size_t size, const media_raw_audio_format &format)
{
	// We're going to be cheap and only work for floating-point audio 
	if (format.format != media_raw_audio_format::B_AUDIO_FLOAT) { 
		return; 
	}
	
// assumes 44.1Khz stereo floats

	bool stop_needed = false;
	size_t i; 
	float *buf = (float *) buffer; 
	size_t float_size = size/4; 
	cookie_record *cookie = (cookie_record *) theCookie;
	float left = 0.0, right = 0.0;
	double fraq = fabs(Pool.frequency/Pool.system_frequency);
	// позиция в кадрах на начало буфера - по ней микшер подмешивает остальные дорожки
	double mix_start = 0;
	if (cookie->tailing)
		mix_start = cookie->tail_pos;
	else if (Pool.sample_type != NONE && Pool.sample_memory)
		mix_start = (double)((cookie->mem - Pool.sample_memory) / Pool.sample_type) + cookie->add;
	const double mix_step = ((Pool.frequency < 0) ? -1.0 : 1.0) * fraq;	// кадров активной дорожки на кадр выхода
	
	if ((Pool.selection == NONE) | cookie->end){
		cookie->end_mem = Pool.sample_memory + Pool.size*Pool.sample_type;
	}else{
		cookie->end_mem = Pool.sample_memory + Pool.r_sel_pointer*Pool.sample_type;
	}
	
	// Now fill the buffer with sound! 

	if (cookie->pause || cookie->tailing){
		for (i=0; i<float_size; i++) { 
			buf[i] = 0.0;
		}
	}else{
		if (Pool.sample_type == MONO){	//cookie->mono){
			for (i=0; i<float_size; i+=2){
				if (cookie->mem >= cookie->end_mem){
					if (cookie->loop){
						cookie->mem = Pool.sample_memory + Pool.pointer*Pool.sample_type;
					}else{
						buf[i] = 0.0;
						buf[i+1] = 0.0;
						stop_needed = true;
					}
				}
				if (!stop_needed){
					buf[i] = *cookie->mem;
					buf[i+1] = *cookie->mem;

					cookie->add += fraq;
					if (Pool.frequency>=0){
						while (cookie->add >= 1.0){
							cookie->mem++;
							--cookie->add;
						}
					}else{
						while (cookie->add >= 1.0){
							cookie->mem--;
							--cookie->add;
						}
						if (cookie->mem < Pool.sample_memory)
							cookie->mem += (cookie->end_mem - Pool.sample_memory);
					}
				}
			}
		}else{
			for (i=0; i<float_size; i+=2) { 
				if (cookie->mem >= cookie->end_mem){
					if (cookie->loop){
						cookie->mem = Pool.sample_memory + Pool.pointer*Pool.sample_type;
					}else{
						buf[i] = 0.0;
						buf[i+1] = 0.0;
						stop_needed = true;
					}
				}
				if (!stop_needed){
					switch(Pool.selection){
					case NONE:
					case BOTH:
						buf[i] = cookie->mem[0];
						buf[i+1] = cookie->mem[1];
						break;
					case LEFT:
						buf[i] = cookie->mem[0];
						buf[i+1] = cookie->mem[0];
						break;
					case RIGHT:
						buf[i] = cookie->mem[1];
						buf[i+1] = cookie->mem[1];
						break;
					}

					cookie->add += fraq;
					if (Pool.frequency>=0){
						while (cookie->add >= 1.0){
							cookie->mem+= 2;
							--cookie->add;
						}
					}else{
						while (cookie->add >= 1.0){
							cookie->mem-= 2;
							--cookie->add;
						}
						if (cookie->mem < Pool.sample_memory)
							cookie->mem += (cookie->end_mem - Pool.sample_memory);
					}
					
				}
			}
		}
	}

	Pool.last_pointer = (cookie->mem - Pool.sample_memory);	// set the last played location
	if (Pool.sample_type == STEREO)
		Pool.last_pointer >>= 1;

	// Активная дорожка закончилась, а другие ещё звучат - продолжаем «хвостом»,
	// пока не кончится самая длинная (только при обычном проигрывании вперёд до конца файла).
	if (cookie->tailing || stop_needed){
		const bool wasTail = cookie->tailing;
		const bool whole = (Pool.selection == NONE) || cookie->end;
		const double aRate = (fabs(Pool.frequency) > 1.0) ? fabs(Pool.frequency) : 44100.0;
		double next = mix_start + (double)(float_size/2) * mix_step;
		double t_next = next / aRate;			// Pool лежит на ленте целиком, смещения нет
		if (Pool.play_mode == RECORD){
			// идёт запись: остальные дорожки играют, остановка - только по кнопке «Стоп»
			cookie->tailing = true;
			cookie->tail_pos = next;
			stop_needed = false;
		}else if (whole && Pool.frequency > 0 && !cookie->loop && t_next < Mix.EndTimeExtras()){
			cookie->tailing = true;
			cookie->tail_pos = next;
			stop_needed = false;
		}else{
			cookie->tailing = false;
			if (wasTail) stop_needed = true;	// хвост закончился
		}
	}

	if (stop_needed)
		Pool.mainWindow->PostMessage(TRANSPORT_STOP);

	// микшер: параметры активной дорожки (громкость/баланс/mute/solo) + остальные дорожки
	Mix.Process(buf, (int)(float_size/2), mix_start, Pool.frequency, Pool.system_frequency,
		!cookie->pause);

	for (int i=0; i<PLAY_HOOKS; i++){
		if (Pool.BufferHook[i]){
			(Pool.BufferHook[i])(buf, float_size, Pool.BufferCookie[i]);
		}
	}
	

	for (i=0; i<float_size; i+=2) { 
		left = MAX(buf[i], left);
		right = MAX(buf[i+1],right);
	}
	cookie->left = left;
	cookie->right = right;

	// update the visuals
	cookie->count -= size;
	if (cookie->count <0){
		cookie->count = (int)Pool.system_frequency/3;						// 24 times a second
			Pool.mainWindow->PostMessage(UPDATE);
	}
}
#endif

MainWindow::MainWindow(BRect frame)
	:BWindow(frame, NULL ,B_TITLED_WINDOW,B_ASYNCHRONOUS_CONTROLS),
	rec_runner(NULL), fGroupItem(NULL), fLockItem(NULL)
{
	Looper()->SetName("BeAE");

	// create global access
	Pool.mainWindow = this;			// handle to this window

	// set MIME types
	Pool.InstallMimeType();

	// init prefs
	Prefs.Init();
	ClipBoard.Init();				// clipboard init
	Hist.Init();					// Undo init
#ifdef __VM_SYSTEM
	VM.Init();
#endif

	Pool.mouseArrow = new BCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
	Pool.mouseArrowLeft = new BCursor(B_CURSOR_ID_RESIZE_WEST);
	Pool.mouseArrowRight = new BCursor(B_CURSOR_ID_RESIZE_EAST);
	Pool.mousePencil = new BCursor(IMouse_Pencil);
	Pool.mouseMove = new BCursor(B_CURSOR_ID_MOVE);
	Pool.mouseArrowMove = new BCursor(IMouse_MoveArrow);
	Pool.mouseLeftRight = new BCursor(B_CURSOR_ID_RESIZE_EAST_WEST);

	ResizeTo( Prefs.frame.Width(), Prefs.frame.Height() );
	MoveTo( Prefs.frame.left, Prefs.frame.top );

	// Set Language
	Language.SetName(Prefs.lang_name.String());
	
	// Now init the keyBindings
	KeyBind.Init();
	
	// Set up the tool tipper
	// befor we load any other tools or windows
	Pool.progress = new ProgressWindow(BRect(0,0,300,30));
	Pool.PrefWin = new PrefWindow();
	
	// create the player and recorder nodes
	Pool.InitBufferPlayer( 44100 );	

	// This is the Pool .. it loads layers and tools and floaters
	// and all kinds of groovy stuff
	Pool.Init();

	// Set the last filter
	FiltersInit();

	SetTitle(Project_Title().String());

// GUI
	mainMenuBar = NULL;
	AddMenu();

	BRect r(0,0,31,62);				// VU meters
	r.OffsetTo(229,Bounds().bottom - 62);
	AddChild(VU_view = new VUView(r));
	Pool.m_VU_View = VU_view;		// add a pointer for the player

	r.Set(0,0,228,62);				// Transport buttons
	r.OffsetTo(0,Bounds().bottom - 62);
	AddChild(transport_view = new TransportView(r));

	r.Set(0,0,259,62);				// Numers at right
	r.OffsetTo(Bounds().right - 259,Bounds().bottom - 62);
	AddChild(pointer_view = new PointersView(r));

	r = Bounds();					// Toolbar
	r.top += mainMenuBar->Bounds().Height() + 1;
	r.bottom = r.top + 32;
	r.right = 3000;
	AddChild(toolBar = new ToolBarView(r));

	r = Bounds();					// Big LCD
	r.top = r.bottom - 62;
	r.left = 261;
	r.right -= 260;
	AddChild(new BigView(r));		// внутри: большой таймер и окно темпа, по центру


// sample Views
	BRect sv = Bounds();
	sv.top += ((mainMenuBar->Bounds().Height() + 2) +32);
	sv.bottom -= 63;

	r = sv;							// track panel (left)
	r.right = r.left + TRACKPANEL_WIDTH - 1;
	AddChild(new TrackPanelView(r));

	sv.left += TRACKPANEL_WIDTH;	// everything else is shifted right

	r = sv;							// wave display
	r.left += VALUEBAR_WIDTH;
	r.right -= LANES_SCROLLBAR_WIDTH;				// справа - полоса прокрутки дорожек
	r.bottom -= TIMEBAR_HEIGHT + MASTER_STRIP_H;		// ниже - мастер-дорожка
	r.top += INDEXVIEW_HEIGHT;
	LanesView *lanes_view = new LanesView(r);		// стопка полос; в полосе активной дорожки - SampleView
	AddChild(lanes_view);
	{
		BRect sb(r.right + 1, sv.top, r.right + LANES_SCROLLBAR_WIDTH, sv.bottom);	// на всю высоту: от верха индекса до низа шкалы времени
		LanesScrollBar *bar = new LanesScrollBar(sb, lanes_view);
		AddChild(bar);
		lanes_view->SetScrollBar(bar);
	}
	sample_view = new SampleView(BRect(0, 0, r.Width(), r.Height()));
	lanes_view->AddChild(sample_view);
	Pool.m_SampleView = sample_view;	// for the player
	((SampleView*)sample_view)->Init();
	
	r = sv;							// left sample heights
//	r.top += INDEXVIEW_HEIGHT;
	r.right = r.left + VALUEBAR_WIDTH-1;
	AddChild(new ValuesView(r));
	
	r = sv;							// мастер-дорожка (под полосами, над шкалой времени)
	r.left += VALUEBAR_WIDTH;
	r.right -= LANES_SCROLLBAR_WIDTH;
	r.bottom -= TIMEBAR_HEIGHT;
	r.top = r.bottom - MASTER_STRIP_H + 1;
	AddChild(new MasterLaneView(r));

	r = sv;							// bottom timescale
	r.left += VALUEBAR_WIDTH;
	r.right -= LANES_SCROLLBAR_WIDTH;
	r.top = r.bottom - TIMEBAR_HEIGHT+1;
	AddChild(new TimeBarView(r));

	r = sv;							// scroll_view / overview
	r.left += VALUEBAR_WIDTH;
	r.right -= LANES_SCROLLBAR_WIDTH;
	r.bottom = r.top + INDEXVIEW_HEIGHT-1;
	AddChild(index_view = new IndexView(r));
	
//	r = sv;
//	r.bottom = r.top + INDEXVIEW_HEIGHT-1;
//	AddChild(new IndexView(r));


	Tracks_Init();					// 4 пустые дорожки, активная - первая

	Pool.SetLoop(  (transport_view->loop->Value() == B_CONTROL_ON) );
	SetSizeLimits(MIN_W ,MAX_W, MIN_H , MAX_H);
	SetPulseRate(50000);
}

// Файлы, брошенные в окно. Звук ложится клипом на дорожку под курсором (в момент, где бросили, с липкой сеткой);
// брошенный мимо дорожек, а также несколько файлов сразу - на первые пустые или новые дорожки. Файл .bemix - открыть как проект.
void MainWindow::HandleDrop(BMessage *message)
{
	if (Recorder.IsRecording())
		return;

	int lane = -1;
	double t = 0;
	LanesView *lanes = dynamic_cast<LanesView*>(FindView("Lanes view"));
	if (lanes && message->WasDropped()){
		BPoint lp = message->DropPoint();			// экранные координаты
		lanes->ConvertFromScreen(&lp);
		if (lanes->Bounds().Contains(lp)){
			lane = lanes->SlotAtY(lp.y);
			if (Pool.sample_type != NONE && Pool.r_pointer > Pool.l_pointer && Pool.frequency > 1.0f){
				const double frame = Pool.l_pointer + lp.x * (double)(Pool.r_pointer - Pool.l_pointer) / (lanes->Bounds().Width() + 1);
				t = frame / Pool.frequency;
				if (t < 0) t = 0;
				if (lane >= 0) t = Mix.Snap(lane, t);
			}
		}
	}

	int count = 0;
	entry_ref ref;
	while (message->FindRef("refs", count, &ref) == B_OK)
		count++;

	for (int32 i = 0; i < count && message->FindRef("refs", i, &ref) == B_OK; i++){
		BString leaf(ref.name);
		if (leaf.Length() > 6 && strcasecmp(leaf.String() + leaf.Length() - 6, ".bemix") == 0){
			BMessage open(MSG_PROJECT_FILE);			// проект открывается целиком
			open.AddRef("refs", &ref);
			PostMessage(&open);
			return;
		}
		SourceRef src;
		const int status = ImportAudioFile(ref, src);
		if (status == IMPORT_NO_MEMORY){
			(new BAlert(NULL, Language.get("MEM_ERROR"), Language.get("OK")))->Go();
			break;
		}
		if (status != IMPORT_OK){
			(new BAlert(NULL, Language.get("LOADING_NO_AUDIO"), Language.get("OK")))->Go();
			continue;
		}
		BString name(leaf);
		const int32 dot = name.FindLast('.');
		if (dot > 0) name.Truncate(dot);
		if (Tracks_ImportClip(count == 1 ? lane : -1, src, t, name.String()) < 0){
			(new BAlert(NULL, Language.get("DROP_NO_TRACKS"), Language.get("OK")))->Go();
			break;
		}
		Pool.changed = true;
	}
	Pool.UpdateToolBar();
}

void MainWindow::AddMenu()
{
	BMenu		*menu;
	BMenuItem	*menuItem;
	BRect		menuBarRect;
	
	menuBarRect.Set(0.0, 0.0, 10000.0, 18.0);
	mainMenuBar = new MyMenuBar(menuBarRect, "MenuBar");
	AddChild(mainMenuBar);
	
	menu = new BMenu(Language.get("FILE_MENU"));
	mainMenuBar->AddItem(menu);
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_NEW"), new BMessage(NEW), KeyBind.GetKey("FILE_NEW"), KeyBind.GetMod("FILE_NEW")));

	recent_menu = new BMenu(Language.get("FILE_OPEN"));
	UpdateRecent();
	menu->AddItem(recent_menu);
	BMenuItem *openitem = menu->FindItem(Language.get("FILE_OPEN"));
	openitem->SetShortcut(KeyBind.GetKey("FILE_OPEN"),KeyBind.GetMod("FILE_OPEN"));
	openitem->SetMessage(new BMessage(OPEN));
//	openitem->SetShortcut('O', 0);

	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_INSERT"), new BMessage(MIDI_IMPORT), KeyBind.GetKey("FILE_INSERT"), KeyBind.GetMod("FILE_INSERT")));
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_APPEND"), new BMessage(APPEND), KeyBind.GetKey("FILE_APPEND"), KeyBind.GetMod("FILE_APPEND")));
	menuItem->SetEnabled(false);
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_MIX"), new BMessage(OPEN_MIX), KeyBind.GetKey("FILE_MIX"), KeyBind.GetMod("FILE_MIX")));
	menu->AddItem(new BMenuItem(Language.get("FILE_MIX_AS"), new BMessage(MIXDOWN_AS)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(Language.get("PROJECT_NEW"), new BMessage(PROJECT_NEW)));
	menu->AddItem(new BMenuItem(Language.get("PROJECT_OPEN"), new BMessage(PROJECT_OPEN)));
	menu->AddItem(new BMenuItem(Language.get("PROJECT_SAVE"), new BMessage(PROJECT_SAVE)));
	menu->AddItem(new BMenuItem(Language.get("PROJECT_SAVE_AS"), new BMessage(PROJECT_SAVE_AS)));
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_SAVE"), new BMessage(SAVE), KeyBind.GetKey("FILE_SAVE"), KeyBind.GetMod("FILE_SAVE")));
	Pool.mn_save = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_SAVE_AS"), new BMessage(SAVE_AS), KeyBind.GetKey("FILE_SAVE_AS"), KeyBind.GetMod("FILE_SAVE_AS")));
	Pool.mn_save_as = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_SAVE_SELECTION"), new BMessage(SAVE_SELECTION), KeyBind.GetKey("FILE_SAVE_SELECTION"), KeyBind.GetMod("FILE_SAVE_SELECTION")));
	Pool.mn_save_sel = menuItem;
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("PREFERENCES"), new BMessage(PREFERENCES), KeyBind.GetKey("PREFERENCES"), KeyBind.GetMod("PREFERENCES")));
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("FILE_QUIT"), new BMessage(B_QUIT_REQUESTED), KeyBind.GetKey("FILE_QUIT"), KeyBind.GetMod("FILE_QUIT")));

	menu = new BMenu(Language.get("EDIT_MENU"));
	Pool.menu_edit = menu;
	mainMenuBar->AddItem(menu);
	menu->AddItem(menuItem = new BMenuItem(Language.get("UNDO"), new BMessage(UNDO), KeyBind.GetKey("UNDO"), KeyBind.GetMod("UNDO")));
	Pool.mn_undo = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("REDO"), new BMessage(REDO), KeyBind.GetKey("REDO"), KeyBind.GetMod("REDO")));
	Pool.mn_redo = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("UNDO_ENABLE"), new BMessage(UNDO_ENABLE), KeyBind.GetKey("UNDO_ENABLE"), KeyBind.GetMod("UNDO_ENABLE")));
	Pool.mn_undo_enable = menuItem;
	menuItem->SetMarked(Prefs.save_undo);
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("COPY"), new BMessage(B_COPY), KeyBind.GetKey("COPY"), KeyBind.GetMod("COPY")));
	Pool.mn_copy = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("CUT"), new BMessage(B_CUT), KeyBind.GetKey("CUT"), KeyBind.GetMod("CUT")));
	Pool.mn_cut = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("PASTE"), new BMessage(B_PASTE), KeyBind.GetKey("PASTE"), KeyBind.GetMod("PASTE")));
	Pool.mn_paste = menuItem;
#ifdef __SAMPLE_STUDIO_LE
	menu->AddItem(menuItem = new BMenuItem(Language.get("EDIT_PASTE_MIX"), new BMessage(PASTE_MIXED), KeyBind.GetKey("EDIT_PASTE_MIX"), KeyBind.GetMod("EDIT_PASTE_MIX")));
	menuItem->SetEnabled(false);
	Pool.mn_paste_mix = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("COPY_TO_STACK"), new BMessage(TO_STACK), KeyBind.GetKey("COPY_TO_STACK"), KeyBind.GetMod("COPY_TO_STACK")));
	Pool.mn_copy_to_stack = menuItem;
#endif
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("SELECT_ALL"), new BMessage(B_SELECT_ALL), KeyBind.GetKey("SELECT_ALL"), KeyBind.GetMod("SELECT_ALL")));
	Pool.mn_select_all = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("UNSELECT_ALL"), new BMessage(UNSELECT_ALL), KeyBind.GetKey("UNSELECT_ALL"), KeyBind.GetMod("UNSELECT_ALL")));
	Pool.mn_unselect = menuItem;
	menu->AddSeparatorItem();
	BMenu *sub = new BMenu(Language.get("ZERO_CROSS"));
	Pool.menu_zero = sub;
	sub->AddItem(new BMenuItem(Language.get("ZERO_IN"), new BMessage(ZERO_IN), KeyBind.GetKey("ZERO_IN"), KeyBind.GetMod("ZERO_IN")));
	sub->AddItem(new BMenuItem(Language.get("ZERO_OUT"), new BMessage(ZERO_OUT), KeyBind.GetKey("ZERO_OUT"), KeyBind.GetMod("ZERO_OUT")));
	sub->AddItem(new BMenuItem(Language.get("ZERO_LL"), new BMessage(ZERO_LL), KeyBind.GetKey("ZERO_LL"), KeyBind.GetMod("ZERO_LL")));
	sub->AddItem(new BMenuItem(Language.get("ZERO_LR"), new BMessage(ZERO_LR), KeyBind.GetKey("ZERO_LR"), KeyBind.GetMod("ZERO_LR")));
	sub->AddItem(new BMenuItem(Language.get("ZERO_RL"), new BMessage(ZERO_RL), KeyBind.GetKey("ZERO_RL"), KeyBind.GetMod("ZERO_RL")));
	sub->AddItem(new BMenuItem(Language.get("ZERO_RR"), new BMessage(ZERO_RR), KeyBind.GetKey("ZERO_RR"), KeyBind.GetMod("ZERO_RR")));
	menu->AddItem(sub);

	// клипы активной дорожки
	sub = new BMenu(Language.get("CLIP_MENU"));
	sub->AddItem(new BMenuItem(Language.get("CLIP_PASTE_N"), new BMessage(CLIP_PASTE_N), KeyBind.GetKey("CLIP_PASTE_N"), KeyBind.GetMod("CLIP_PASTE_N")));
	sub->AddItem(new BMenuItem(Language.get("CLIP_DELETE"), new BMessage(CLIP_DELETE)));
	sub->AddSeparatorItem();
	sub->AddItem(fLockItem = new BMenuItem(Language.get("CLIP_LOCK"), new BMessage(CLIP_LOCK)));
	sub->AddItem(fGroupItem = new BMenuItem(Language.get("CLIP_GROUP"), new BMessage(CLIP_GROUP)));
	sub->AddItem(new BMenuItem(Language.get("CLIP_MOVE"), new BMessage(TOOL_MOVE)));
	sub->AddSeparatorItem();
	sub->AddItem(new BMenuItem(Language.get("CLIP_SAVE"), new BMessage(CLIP_SAVE)));
	menu->AddItem(sub);
	
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("CLEAR"), new BMessage(CLEAR), KeyBind.GetKey("CLEAR"), KeyBind.GetMod("CLEAR")));
	Pool.mn_clear = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("TRIM"), new BMessage(TRIM), KeyBind.GetKey("TRIM"), KeyBind.GetMod("TRIM")));
	Pool.mn_trim = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("SET_FREQ"), new BMessage(SET_FREQUENCY), KeyBind.GetKey("SET_FREQ"), KeyBind.GetMod("SET_FREQ")));
	Pool.mn_set_freq = menuItem;
	menu->AddItem(menuItem = new BMenuItem(Language.get("RESAMPLE"), new BMessage(RESAMPLE), KeyBind.GetKey("RESAMPLE"), KeyBind.GetMod("RESAMPLE")));
	Pool.mn_resample = menuItem;

	menu = new BMenu(Language.get("TRANSFORM_MENU"));
	Pool.menu_transform = menu;
	mainMenuBar->AddItem(menu);

	menu = new BMenu(Language.get("ANALYZE_MENU"));
	Pool.menu_analyze = menu;
	mainMenuBar->AddItem(menu);

	#ifdef __SAMPLE_STUDIO_LE
		menu = new BMenu(Language.get("GENERATE_MENU"));
		Pool.menu_generate = menu;
		mainMenuBar->AddItem(menu);
	#endif

	menu = new BMenu(Language.get("HELP_MENU"));
	mainMenuBar->AddItem(menu);
	menu->AddItem(menuItem = new BMenuItem(Language.get("HELP"), new BMessage(HELP), KeyBind.GetKey("HELP"), KeyBind.GetMod("HELP")));
//	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("HISTORY"), new BMessage(HISTORY), KeyBind.GetKey("HISTORY"), KeyBind.GetMod("HISTORY")));
	menu->AddItem(menuItem = new BMenuItem(Language.get("HOMEPAGE"), new BMessage(HOMEPAGE), KeyBind.GetKey("HOMEPAGE"), KeyBind.GetMod("HOMEPAGE")));
	menu->AddSeparatorItem();
	menu->AddItem(menuItem = new BMenuItem(Language.get("ABOUT"), new BMessage(ABOUT), KeyBind.GetKey("ABOUT"), KeyBind.GetMod("ABOUT")));

	SetKeyMenuBar(NULL);
}

MainWindow::~MainWindow()
{
}

void MainWindow::MenusBeginning()
{
	Pool.UpdateMenu();
	if (fLockItem) fLockItem->SetMarked(Tracks_SelectionLocked());
	if (fGroupItem){
		fGroupItem->SetMarked(Tracks_SelectionGrouped());
		fGroupItem->SetEnabled(Tracks_SelectedCount() >= 2);
	}
}

void MainWindow::UpdateRecent()
{
	BMenuItem *menuItem;
	BMessage msg;
	BMessage *msgout = NULL;
	entry_ref eref;
	BEntry e;
	int32 i = 0;
	char name[B_FILE_NAME_LENGTH];
	
	while(recent_menu->ItemAt(0))
		recent_menu->RemoveItem(recent_menu->ItemAt(0));

	be_roster->GetRecentDocuments(&msg,10,"audio");
	while(msg.FindRef("refs",i,&eref) == B_OK){
		e.SetTo(&eref);
		if(e.InitCheck() == B_OK){
			e.GetName(name);
			msgout = new BMessage(B_REFS_RECEIVED);//DO_OPEN);
			msgout->AddRef("refs",&eref);
			recent_menu->AddItem(menuItem = new BMenuItem(name,msgout));
			menuItem->SetTarget(be_app);
		}
		i++;
	}
}

/*******************************************************
*   Запись с аудиовхода
*******************************************************/
void MainWindow::UpdateRecordingTitle()
{
	float rate = Recorder.Rate();
	int32 secs = (rate > 0) ? (int32)(Recorder.Frames() / rate) : 0;
	char s[B_FILE_NAME_LENGTH + 64];
	sprintf(s, "BeAE - * %s %02d:%02d", Language.get("RECORDING"), (int)(secs / 60), (int)(secs % 60));
	SetTitle(s);
}

void MainWindow::StartRecording()
{
	if (Recorder.IsRecording())
		return;

	// остановить воспроизведение
	if (Pool.IsPlaying()){
		transport_view->play->SetValue(B_CONTROL_OFF);
		transport_view->play_sel->SetValue(B_CONTROL_OFF);
		transport_view->stop->SetValue(B_CONTROL_ON);
		play_cookie.mem = play_cookie.start_mem;
		Pool.StopPlaying();
	}

	// запись идёт только в дорожку с кнопкой R
	int armed = Mix.ArmedTrack();
	if (armed < 0){
		(new BAlert(NULL, Language.get("REC_ERR_NO_ARMED"), Language.get("OK")))->Go();
		return;
	}
	if (armed != Mix.Active()){
		if (!Tracks_Activate(armed))
			return;
	}

	// старт записи на общей шкале времени: позиция курсора в активной дорожке + её смещение
	{
		rec_old_offset = 0;						// Pool лежит на ленте целиком
		double rate = (fabs(Pool.frequency) > 1.0) ? fabs(Pool.frequency) : 44100.0;
		rec_tail_start = (Pool.sample_type != NONE) ? (double)Pool.pointer : 0.0;
		rec_start_time = rec_old_offset + rec_tail_start / rate;
	}

	BString error;
	status_t err = Recorder.Start(&error);
	if (err != B_OK){
		BString text(Language.get(error.String()));
		text << "\n(" << strerror(err) << ")";
		(new BAlert(NULL, text.String(), Language.get("OK")))->Go();
		return;
	}

	Pool.play_mode = RECORD;
	Pool.StartTailPlaying(rec_tail_start);		// остальные дорожки играют во время записи
	rec_old_title = Title();
	UpdateRecordingTitle();
	delete rec_runner;
	rec_runner = new BMessageRunner(BMessenger(this), new BMessage(TRANSPORT_REC_TICK), 250000);
}

void MainWindow::StopRecording()
{
	delete rec_runner;
	rec_runner = NULL;
	Recorder.Stop();
	Pool.StopPlaying();					// остановить воспроизведение остальных дорожек
	play_cookie.tailing = false;
	FinishRecording();
}

// Превращает записанное в новый файл (так же, как это делает загрузка файла).
void MainWindow::FinishRecording()
{
	Pool.play_mode = NONE;

	int64 frames = Recorder.Frames();
	const int32 channels = Recorder.Channels();
	const float rate = Recorder.Rate();

	if (frames < 2){				// слишком короткая запись, ничего не делаем
		Recorder.Clear();
		SetTitle(rec_old_title.String());	// вернуть прежний заголовок
		return;
	}

	float *mem = Recorder.CopyData(256);	// 256 float запаса, как «+1024» при загрузке файла
	if (mem == NULL){
		Recorder.Clear();
		(new BAlert(NULL, Language.get("MEM_ERROR"), Language.get("OK")))->Go();
		return;
	}

	uint32 srcFormat = Recorder.SourceFormat();
	Recorder.Clear();

	// компенсация задержки: вход приходит позже воспроизведения, отбрасываем начало записи
	{
		int64 skip = (int64)((double)Prefs.rec_latency_ms * rate / 1000.0);
		if (skip > 0 && skip < frames - 2){
			memmove(mem, mem + skip * channels, (size_t)((frames - skip) * channels) * sizeof(float));
			frames -= skip;
		}
	}

	// undo: прежнее содержимое дорожки и её смещение (без предупреждений о замене)
	if (Pool.sample_type != NONE && Pool.size > 0)
		Hist.Save(H_FULL, 0, Pool.size);
	else
		Hist.Save(H_EMPTY, 0, 0);
	Hist.SetOffsetUndo(Mix.Active(), rec_old_offset);

	Pool.sample_type = NONE;		// как при загрузке: блокируем отрисовку на время замены
	bool temp_pause = play_cookie.pause;
	play_cookie.pause = true;

#ifndef __VM_SYSTEM	// RAM
	if (Pool.sample_memory)
		free(Pool.sample_memory);
	Pool.sample_memory = mem;
#else
	VM.Reset();
	VM.WriteBlock(mem, frames * channels);
	free(mem);
#endif

	// формат для сохранения: тот, что дал вход (16 бит, 24/32 бита или float)
	Pool.m_format = media_format();		// конструктор обнуляет структуру
	Pool.m_format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format &raw = Pool.m_format.u.raw_audio;
	raw.frame_rate = rate;
	raw.channel_count = channels;
	switch (srcFormat){
	case media_raw_audio_format::B_AUDIO_INT:
	case media_raw_audio_format::B_AUDIO_FLOAT:
	case media_raw_audio_format::B_AUDIO_SHORT:
		raw.format = srcFormat;
		break;
	default:
		raw.format = media_raw_audio_format::B_AUDIO_SHORT;	// 8-битные входы сохраняем как 16 бит
		break;
	}
	raw.byte_order = B_MEDIA_HOST_ENDIAN;
	raw.buffer_size = (raw.format & 0xf) * channels * 2048;
	Pool.sample_bits = (raw.format & 0xf) * 8;

	Pool.frequency = rate;
	Pool.size = frames - 1;				// как при загрузке: последний кадр не считается
	Pool.sample_type = (channels == 1) ? MONO : STEREO;
	Tracks_PlaceActive(rec_start_time);			// запись ложится на ленту там, где стоял курсор
	Pool.selection = NONE;
	Pool.r_pointer = Pool.size;
	Pool.pointer = 0;
	Pool.r_sel_pointer = Pool.pointer;
	Pool.l_pointer = 0;

#ifndef __VM_SYSTEM	// RAM
	play_cookie.mem = Pool.sample_memory;
	play_cookie.start_mem = Pool.sample_memory;
	play_cookie.end_mem = Pool.sample_memory + Pool.size * Pool.sample_type;
#else
	play_cookie.mem = 0;
	play_cookie.start_mem = 0;
#endif
	play_cookie.frequency = Pool.frequency;
	play_cookie.add = 0;

	Pool.changed = true;				// запись ещё не сохранена
	Pool.ResetIndexView();

	SetTitle(Project_Title().String());

	Pool.sample_view_dirty = true;
	Pool.update_draw_cache = true;
	Pool.update_index = true;
	Pool.update_peak = true;
	Pool.RedrawWindow();
	Pool.InitBufferPlayer(Pool.frequency);

	play_cookie.pause = temp_pause;
	Pool.UpdateToolBar();
}

bool MainWindow::QuitRequested()
{
	if (Recorder.IsRecording())
		StopRecording();		// запись превратится в файл, ниже спросим про сохранение

	if (!Pool.IsChanged(2)){
		Prefs.frame = Frame();
		Pool.StopPlaying();
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}else{
		return false;
	}
}

void MainWindow::MessageReceived(BMessage *message)
{
	RealtimeFilter *filter = NULL;
	int32 x, key, mod, raw_key;
	uint32 msg;
	float y;

//	message->PrintToStream();

	switch (message->what){
	case CHANGE_LANGUAGE:
		if (mainMenuBar){
			mainMenuBar->RemoveSelf();
			delete mainMenuBar;
		}
		AddMenu();
		break;

	case TRANSPORT_REC:
		if (Recorder.IsRecording())
			StopRecording();
		else
			StartRecording();
		break;

	case TRANSPORT_REC_TICK:
		if (Recorder.IsRecording()){
			// ограничение по памяти: ~1 ГБ данных, затем автоматическая остановка
			if ((int64)Recorder.Frames() * Recorder.Channels() > (int64)256*1024*1024)
				StopRecording();
			else
				UpdateRecordingTitle();
		}
		break;

	case TRANSPORT_PAUSE_MAN:
		if (transport_view->pause->Value() == B_CONTROL_ON)
			transport_view->pause->SetValue(B_CONTROL_OFF);
		else
			transport_view->pause->SetValue(B_CONTROL_ON);
		// no break as TRANSPORT_PAUSE needs to follow
	case TRANSPORT_PAUSE:
		play_cookie.pause = (transport_view->pause->Value() == B_CONTROL_ON);
		break;

	case TRANSPORT_TOGGLE:
		if (Pool.IsPlaying())	PostMessage(TRANSPORT_STOP);
		else					PostMessage(TRANSPORT_PLAYS);
		break;
	
	case TRANSPORT_SET:
		Pool.pointer = Pool.last_pointer;
		Pool.sample_view_dirty = true;	// update the sample-view
		Pool.RedrawWindow();
		break;

	case TRANSPORT_PLAY:
		if (Recorder.IsRecording())	break;
		if (Pool.size == 0)	break;
		if (Pool.sample_type == NONE){
			transport_view->play_sel->SetValue(B_CONTROL_OFF);
			transport_view->play->SetValue(B_CONTROL_OFF);
			break;
		}
		transport_view->stop->SetValue(B_CONTROL_OFF);
		transport_view->play->SetValue(B_CONTROL_OFF);
		transport_view->play_sel->SetValue(B_CONTROL_ON);

//		Pool.SetLoop(  (transport_view->loop->Value() == B_CONTROL_ON) );
		Pool.StartPlaying(Pool.pointer*Pool.sample_type, true);	// play till end
		break;

	case TRANSPORT_PLAYS:
		if (Recorder.IsRecording())	break;
		if (Pool.size == 0)	break;
		if (Pool.sample_type == NONE){
			transport_view->play_sel->SetValue(B_CONTROL_OFF);
			transport_view->play->SetValue(B_CONTROL_OFF);
			break;
		}
		transport_view->stop->SetValue(B_CONTROL_OFF);
		transport_view->play_sel->SetValue(B_CONTROL_OFF);
		transport_view->play->SetValue(B_CONTROL_ON);

//		Pool.SetLoop(  (transport_view->loop->Value() == B_CONTROL_ON) );
		Pool.StartPlaying(Pool.pointer*Pool.sample_type, false);
		break;

	case TRANSPORT_STOP:
		if (Recorder.IsRecording()){	// «Стоп» во время записи завершает запись
			StopRecording();
			break;
		}
		transport_view->play->SetValue(B_CONTROL_OFF);
		transport_view->play_sel->SetValue(B_CONTROL_OFF);
		transport_view->stop->SetValue(B_CONTROL_ON);
		play_cookie.mem = play_cookie.start_mem;
		Pool.StopPlaying();
		FindView("Sample view")->Pulse();
		FindView("Index view")->Invalidate();
		break;

	case TRANSPORT_LOOP_MAN:
		if (transport_view->loop->Value() == B_CONTROL_ON)
			transport_view->loop->SetValue(B_CONTROL_OFF);
		else
			transport_view->loop->SetValue(B_CONTROL_ON);
		// no break as TRANSPORT_LOOP needs to follow
	case TRANSPORT_LOOP:
		Pool.SetLoop(  (transport_view->loop->Value() == B_CONTROL_ON) );
		break;

	case OPEN:
		if (Recorder.IsRecording())	break;
		if (!Pool.IsChanged())
			((MyApplication*)be_app)->fOpenPanel->Show();
		break;
	
	case SAVE:			// need to add default setting in the save-panel for this
	case SAVE_AS:
		if (Pool.sample_type == NONE)	return;
		Pool.save_selection = false;
		((MyApplication*)be_app)->fSavePanel->Window()->SetTitle(Language.get("PANEL_SAVE"));
		((MyApplication*)be_app)->fSavePanel->Show();
		break;
	
	case SAVE_SELECTION:
		if (Pool.selection == NONE || Pool.sample_type == NONE)	return;
		((MyApplication*)be_app)->fSavePanel->Window()->SetTitle(Language.get("PANEL_SAVE_SELECTION"));
		Pool.save_selection = true;
		((MyApplication*)be_app)->fSavePanel->Show();
		break;
	
	case UNDO:
		Pool.Undo();
		break;

	case REDO:
		if (Tracks_Redo()) Pool.changed = true;
		Pool.UpdateToolBar();
		break;
		
	case B_SELECT_ALL:
		if (Pool.size == 0)	break;
		Pool.SelectAll();
		break;

	case UNSELECT_ALL:
		if (Pool.size == 0)	break;
		Pool.DeSelectAll();
		break;

	case DROP_PASTE:
	{
		BPoint p;
		message->FindPoint("_drop_point_", &p);
		p = sample_view->ConvertFromScreen(p);
		BRect r = sample_view->Bounds();
		if (r.Contains(p)){
			Pool.selection = NONE;
			Pool.pointer = (int32)(Pool.l_pointer + p.x * (Pool.r_pointer - Pool.l_pointer)/Bounds().Width());
			ClipBoard.Paste();
		}
	}	break;
	
	case PASTE_MIXED:
		ClipBoard.PasteMix();		// ClipBoard handles redrawing and dialogs
		break;

	case B_MOUSE_WHEEL_CHANGED:
		message->FindFloat("be:wheel_delta_y", &y);
		if (y==-1)	PostMessage(ZOOM_IN);
		if (y==1)	PostMessage(ZOOM_OUT);
		break;

	case ZOOM_IN:
		if (Pool.size == 0)	break;
		x = Pool.r_pointer - Pool.l_pointer;
		
		if (x < Pool.m_SampleView->Bounds().Width()/64)	break;
		
		x /= 2;
		if (x < 1)	x = 1;

		Pool.l_pointer = Pool.l_pointer +x/2;				// window to selection
		if (Pool.l_pointer<0)	Pool.l_pointer = 0;
		Pool.r_pointer = Pool.l_pointer + x;
		if (Pool.r_pointer > Pool.size){
			Pool.r_pointer = Pool.size;
			Pool.l_pointer = Pool.r_pointer - x;
			if (Pool.l_pointer<0)
				Pool.l_pointer = 0;
		}
		Pool.update_index = true;
		Pool.UpdateToolBar();
		Pool.RedrawWindow();
		break;
	case ZOOM_OUT:
		if (Pool.size == 0)	break;
		x = (Pool.r_pointer - Pool.l_pointer)+1;
		x *= 2;
		if (x > Pool.size)	x = Pool.size;

		Pool.l_pointer = Pool.l_pointer -x/4;				// window to selection
		if (Pool.l_pointer<0)	Pool.l_pointer = 0;
		Pool.r_pointer = Pool.l_pointer + x;
		if (Pool.r_pointer > Pool.size){
			Pool.r_pointer = Pool.size;
			Pool.l_pointer = Pool.r_pointer - x;
			if (Pool.l_pointer<0)
				Pool.l_pointer = 0;
		}
		Pool.UpdateToolBar();
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	case ZOOM_FULL:
		if (Pool.size == 0)	break;
		Pool.l_pointer = 0;
		Pool.r_pointer = Pool.size;
		Pool.UpdateToolBar();
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	case ZOOM_SELECTION:
		if (Pool.size == 0)	break;
		if (Pool.selection != NONE){
			Pool.l_pointer = Pool.pointer;
			Pool.r_pointer = Pool.r_sel_pointer;
		}
		Pool.UpdateToolBar();
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	case ZOOM_LEFT:
		if (Pool.size == 0 || Pool.selection==NONE)	break;
		x = sample_view->Bounds().IntegerWidth()/6;
		Pool.l_pointer = Pool.pointer -x/2;				// window to selection
		if (Pool.l_pointer<0)	Pool.l_pointer = 0;
		Pool.r_pointer = Pool.l_pointer + x;

		Pool.UpdateToolBar();
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	case ZOOM_RIGHT:
		if (Pool.size == 0 || Pool.selection==NONE)	break;
		x = sample_view->Bounds().IntegerWidth()/6;
		Pool.l_pointer = Pool.r_sel_pointer - x/2;		// window to selection
		if (Pool.l_pointer<0)	Pool.l_pointer = 0;
		Pool.r_pointer = Pool.l_pointer + x;
		if (Pool.r_pointer > Pool.size){
			Pool.r_pointer = Pool.size;
			Pool.l_pointer = Pool.r_pointer - x;
		}

		Pool.UpdateToolBar();
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
		
	case EDIT_L:
		if (Pool.selection != NONE)
			Pool.selection = LEFT;
		Pool.UpdateToolBar();
		sample_view->Draw(sample_view->Bounds());
		break;	
	case EDIT_R:
		if (Pool.selection != NONE)
			Pool.selection = RIGHT;
		Pool.UpdateToolBar();
		sample_view->Draw(sample_view->Bounds());
		break;	
	case EDIT_B:
		if (Pool.selection != NONE)
			Pool.selection = BOTH;
		Pool.UpdateToolBar();
		sample_view->Draw(sample_view->Bounds());
		break;	

	case TRANSPORT_REW:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.pointer -= x/40;
		if (Pool.pointer <0)	Pool.pointer = 0;
		if (Pool.pointer < Pool.l_pointer){
			Pool.l_pointer -= x/10;
			if (Pool.l_pointer <0)	Pool.l_pointer = 0;
		}
		Pool.r_pointer = Pool.l_pointer + x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;

	case TRANSPORT_REW_ALL:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.pointer = 0;
		Pool.l_pointer = 0;
		Pool.r_pointer = x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;

	case TRANSPORT_FWD:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.pointer += x/40;
		if (Pool.pointer >Pool.size)	Pool.pointer = Pool.size;
		if (Pool.pointer > Pool.r_pointer){
			Pool.r_pointer += x/10;
			if (Pool.r_pointer >Pool.size)	Pool.r_pointer = Pool.size;
		}
		Pool.l_pointer = Pool.r_pointer - x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;

	case TRANSPORT_FWD_ALL:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.pointer = Pool.size;;
		Pool.r_pointer = Pool.pointer;
		Pool.l_pointer = Pool.pointer - x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	
	case TRANSPORT_HOME:
		Pool.pointer = Pool.l_pointer;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	
	case TRANSPORT_END:
		Pool.pointer = Pool.r_pointer;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	
	case TRANSPORT_LEFT:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.l_pointer -= x/2;
		if (Pool.l_pointer<0)	Pool.l_pointer = 0;
		Pool.r_pointer = Pool.l_pointer + x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;

	case TRANSPORT_RIGHT:
		x = Pool.r_pointer - Pool.l_pointer;
		Pool.l_pointer += x/2;
		if (Pool.l_pointer>(Pool.size-x))	Pool.l_pointer = Pool.size-x;
		Pool.r_pointer = Pool.l_pointer + x;
		Pool.update_index = true;
		Pool.RedrawWindow();
		break;
	
	case UNDO_ENABLE:
		Prefs.save_undo = !Prefs.save_undo;
		Pool.UpdateToolBar();
		break;
	
	case ABOUT:
		Pool.DoAbout();
		break;
		
	case HELP:
{
		BPath path;
		app_info ai;
		be_app->GetAppInfo(&ai);
		BEntry entry(&ai.ref);
		entry.GetPath(&path);
		path.GetParent(&path);
		path.Append("Help/help.html");
		char *help = new char[strlen(path.Path())+1];
		sprintf(help, path.Path());
		be_roster->Launch("text/html",1, &help);
		delete help;
}		break;
		
	case OPEN_MIX:
	{
		if (Recorder.IsRecording()) break;
		BPath def;
		if (!Project_MixdownDefault(def)){
			PostMessage(MIXDOWN_AS);			// проект ещё не на диске - спросить, куда
			break;
		}
		BEntry e(def.Path());
		if (e.Exists()){
			int32 k = (new BAlert(NULL, Language.get("MIXDOWN_OVERWRITE"), Language.get("OK"), Language.get("CANCEL")))->Go();
			if (k != 0) break;
		}
		BString err;
		if (!Project_Mixdown(def.Path(), err))
			(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		break;
	}

	case MIXDOWN_AS:
		if (Recorder.IsRecording()) break;
		if (gMixdownPanel == NULL){
			gMixdownPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL, 0, false,
				new BMessage(MSG_MIXDOWN_FILE));
			gMixdownPanel->Window()->SetTitle(Language.get("FILE_MIX_AS"));
		}
		{
			BString n(Project_Name()[0] ? Project_Name() : Language.get("MIXDOWN_DEFAULT_NAME"));
			n << ".wav";
			gMixdownPanel->SetSaveText(n.String());
		}
		gMixdownPanel->Show();
		break;

	case MSG_MIXDOWN_FILE:
	{
		entry_ref dir;
		const char *name = NULL;
		if (message->FindRef("directory", &dir) != B_OK || message->FindString("name", &name) != B_OK) break;
		BPath p(&dir);
		p.Append(name);
		BString err;
		if (!Project_Mixdown(p.Path(), err))
			(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		break;
	}

	case B_CUT:					// «Вырезать» = разрезать клип по курсору (исходный файл не трогаем)
	case CLIP_SPLIT:
		if (Tracks_SplitAtCursor()) Pool.changed = true;
		break;
	case B_COPY:				// «Копировать» = клип под курсором в буфер клипов
	case CLIP_COPY:
		Tracks_CopyClip();
		Pool.UpdateToolBar();
		Pool.UpdateMenu();
		break;
	case B_PASTE:				// «Вставить» = один раз в позицию курсора
	case CLIP_PASTE:
	case MSG_CLIP_COUNT:
	{
		int count = 1;
		if (message->what == MSG_CLIP_COUNT){
			const char *s = NULL;
			if (message->FindString("name", &s) != B_OK) break;
			count = atoi(s);
			if (count < 1) count = 1;
			if (count > 200) count = 200;
		}
		const double at = (Pool.sample_type != NONE && Pool.frequency > 1.0f) ? (double)Pool.pointer / Pool.frequency : 0.0;
		if (!Tracks_PasteFits()){
			(new BAlert(NULL, Language.get("CLIP_PASTE_NO_TRACKS"), Language.get("OK")))->Go();
			break;
		}
		if (Tracks_PasteClip(at, count)) Pool.changed = true;
		Pool.UpdateToolBar();
		break;
	}
	case CLIP_SAVE:
		if (Pool.sample_type == NONE || Recorder.IsRecording()) break;
		if (gClipSavePanel == NULL){
			gClipSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL, 0, false, new BMessage(MSG_CLIP_FILE));
			gClipSavePanel->Window()->SetTitle(Language.get("SAVE_CLIP_TITLE"));
		}
		{
			BString n(Mix.Track(Mix.Active()) ? Mix.Track(Mix.Active())->name : "clip");
			n << "_clip.wav";
			gClipSavePanel->SetSaveText(n.String());
		}
		gClipSavePanel->Show();
		break;

	case MSG_CLIP_FILE:
	{
		entry_ref dir;
		const char *name = NULL;
		if (message->FindRef("directory", &dir) != B_OK || message->FindString("name", &name) != B_OK) break;
		BPath p(&dir);
		p.Append(name);
		BString err;
		if (!Tracks_SaveActiveClip(p.Path(), err))
			(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		break;
	}

	case CLIP_PASTE_N:
		if (Tracks_HasClipboard() && !Recorder.IsRecording())
			(new ProjectNameWindow(this, MSG_CLIP_COUNT, "2", Language.get("CLIP_COUNT_TITLE"), Language.get("CLIP_COUNT_LABEL")))->Show();
		break;
	case CLIP_DELETE:
		if (Tracks_DeleteClip()) Pool.changed = true;
		break;
	case CLIP_GROUP:
		if (!Recorder.IsRecording() && Tracks_ToggleSelectionGroup()){
			Pool.changed = true;
			Pool.sample_view_dirty = true;
			Pool.RedrawWindow();
		}
		break;
	case CLIP_LOCK:
		if (Pool.sample_type != NONE && !Recorder.IsRecording()){
			Tracks_ToggleSelectionLock();
			Pool.changed = true;
			Pool.sample_view_dirty = true;
			Pool.RedrawWindow();
		}
		break;

	case PROJECT_NEW:
		if (Recorder.IsRecording() || Pool.IsChanged()) break;
		Tracks_BeginProject();
		Project_Forget();
		Tracks_Init();
		Tracks_FinishProject();
		SetTitle(Project_Title().String());
		break;

	case PROJECT_SAVE:
	case PROJECT_SAVE_AS:
	{
		if (Recorder.IsRecording()) break;
		if (message->what == PROJECT_SAVE && Project_HasFolder()){
			BString err;
			if (!Project_SaveAgain(err))
				(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
			else
				SetTitle(Project_Title().String());
			break;
		}
		if (gProjFolderPanel == NULL){
			BMessage *m = new BMessage(MSG_PROJECT_FOLDER);
			gProjFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL, B_DIRECTORY_NODE, false, m);
			gProjFolderPanel->Window()->SetTitle(Language.get("PROJECT_FOLDER_PANEL"));
		}
		gProjFolderPanel->Show();
		break;
	}

	case MSG_PROJECT_FOLDER:
	{
		entry_ref ref;
		if (message->FindRef("refs", &ref) == B_OK){
			BEntry e(&ref);
			if (e.GetPath(&gProjParent) == B_OK){
				const char *def = Project_Name()[0] ? Project_Name() : Language.get("PROJECT_DEFAULT_NAME");
				(new ProjectNameWindow(this, MSG_PROJECT_NAME, def))->Show();
			}
		}
		break;
	}

	case MSG_PROJECT_NAME:
	{
		const char *name = NULL;
		if (message->FindString("name", &name) != B_OK || gProjParent.InitCheck() != B_OK) break;
		BString err;
		if (!Project_Save(gProjParent, name, err))
			(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		else
			SetTitle(Project_Title().String());
		break;
	}

	case PROJECT_OPEN:
		if (Recorder.IsRecording()) break;
		if (gProjOpenPanel == NULL){
			gProjOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL, B_FILE_NODE, false,
				new BMessage(MSG_PROJECT_FILE));
			gProjOpenPanel->Window()->SetTitle(Language.get("PROJECT_OPEN"));
		}
		gProjOpenPanel->Show();
		break;

	case MSG_PROJECT_FILE:
	{
		entry_ref ref;
		if (message->FindRef("refs", &ref) != B_OK) break;
		BPath p(&ref);
		BString err;
		bool ok = Project_Open(p.Path(), err);
		if (!ok)
			(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		else{
			SetTitle(Project_Title().String());
			if (err.Length() > 0)		// проект открыт, но часть файлов не найдена
				(new BAlert(NULL, err.String(), Language.get("OK")))->Go();
		}
		break;
	}

	case NEW:
	{	app_info info;
		be_app->GetAppInfo(&info);
		be_roster->Launch(info.signature);
	}	break;
		
	case PASTE_NEW:
	{	app_info info;
		be_app->GetAppInfo(&info);
		be_roster->Launch(info.signature, new BMessage(B_PASTE));
	}	break;
		
	case HISTORY:
{
		BPath path;
		app_info ai;
		be_app->GetAppInfo(&ai);
		BEntry entry(&ai.ref);
		entry.GetPath(&path);
		path.GetParent(&path);
		path.Append("Help/history.html");
		char *history = new char[strlen(path.Path())+1];
		sprintf(history, path.Path());
		be_roster->Launch("text/html",1, &history);
		delete history;
}		break;
		
	case HOMEPAGE:
		be_roster->Launch("text/html",1, const_cast<char**>(&HOMEPAGE_URL));
		break;
		
	case PREFERENCES:
		if(Pool.PrefWin != NULL){
			Pool.PrefWin->Show();
		}else{
			//Create a new Prefs windos?!
		}
		break;

	case UPDATE:
		FindView("Sample view")->Pulse();
		FindView("Big view")->Pulse();
		FindView("Index view")->Invalidate();
		break;
	
	case REDRAW:
		Pool.sample_view_dirty = true;	// update the sample-view
		Pool.update_index = true;
		Pool.ResetIndexView();
		Pool.RedrawWindow();
		break;
	
	case TOOL_SELECT:
		Pool.tool_mode = SELECT_TOOL;
		Pool.UpdateToolBar();
		break;

	case TOOL_MOVE:
		Pool.tool_mode = MOVE_TOOL;
		Pool.UpdateToolBar();
		break;

	case TOOL_DRAW:
		Pool.tool_mode = DRAW_TOOL;
		Pool.UpdateToolBar();
		break;

	case TOOL_PLAY:
		Pool.tool_mode = PLAY_TOOL;
		Pool.UpdateToolBar();
		break;

	case TOOL_JOGG:
		Pool.tool_mode = SCRUB_TOOL;
		Pool.UpdateToolBar();
		break;

	case SPECTRUM:
		(new SpectrumWindow());
		break;
		
	case SAMPLE_SCOPE:
		(new SampleScopeWindow());
		break;
		
	case MIDI_IMPORT:
		new MidiImportWindow(BMessenger(this), Tracks_IsEmpty(), Mix.Bpm());
		break;

	case MIDI_IMPORT_DONE:
	{
		MidiImportResult *res = NULL;
		if (message->FindPointer("result", (void **)&res) != B_OK || !res) break;
		if (!res->error.empty()) {
			(new BAlert(NULL, Language.get(res->error.c_str()), Language.get("OK")))->Go();
		} else {
			if (res->setBpm > 0) Mix.SetBpm(res->setBpm);		// темп проекта (Grid и метроном) по MIDI
			std::string fxWarn;
			for (size_t i = 0; i < res->items.size(); i++) {		// каждая MIDI-дорожка - на первую свободную дорожку (при нехватке добавляются)
				const int lane = Tracks_ImportClip(-1, res->items[i].src, 0.0, res->items[i].name.c_str());
				if (lane < 0) {
					(new BAlert(NULL, Language.get("DROP_NO_TRACKS"), Language.get("OK")))->Go();
					break;
				}
				MidiApplyEffects(lane, res->items[i].pan, res->items[i].reverb, res->items[i].chorus, fxWarn);
				Pool.changed = true;
			}
			if (!fxWarn.empty()) {									// не найден плагин реверба/хоруса
				BString msg = Language.get("MIDI_FX_MISSING");
				(new BAlert(NULL, msg.String(), Language.get("OK")))->Go();
			}
			Pool.UpdateToolBar();
		}
		delete res;
		break;
	}

	case TEMPO_ANALYZER:
		(new TempoAnalyzerWindow(BPoint((Frame().left+Frame().right)/2, (Frame().top+Frame().bottom)/2)));
		break;

	case TUNER_OPEN:
		TunerWindow::Open(BPoint((Frame().left+Frame().right)/2, (Frame().top+Frame().bottom)/2));
		break;

	case SET_FREQUENCY:
		(new FreqWindow(BPoint( (Frame().left+Frame().right)/2, (Frame().top+Frame().bottom)/2)));
		break;

	case RESAMPLE_DO:
		DoResample();
		break;

	case RESAMPLE:
		(new ResampleWindow(BPoint( (Frame().left+Frame().right)/2, (Frame().top+Frame().bottom)/2)));
		break;

	case CLEAR:
		if (!Pool.PrepareFilter())	break;
		ClipBoard.DoSilence();
		Pool.changed = true;
		Pool.UpdateToolBar();
		Pool.HideProgress();
		Pool.ResetIndexView();
		Pool.RedrawWindow();
		break;
		
	case RUN_FILTER:							// run a filter with or without GUI
{		if (Pool.size == 0) break;
		const char *tag = NULL;
		if (message->FindInt32("filter", &mod) == B_OK){
			RunFilter(mod);
		}
		else if (message->FindString("language_key", &tag) == B_OK){
			RunFilter(tag);
		}
}		break;
	
	case RUN_LADSPA:							// LADSPA-плагин из меню
{		if (Pool.size == 0) break;
		const char *lfile = NULL, *llabel = NULL;
		if (message->FindString("file", &lfile) == B_OK && message->FindString("label", &llabel) == B_OK)
			RunLadspaFilter(lfile, llabel);
}		break;

	case RESCAN_LADSPA:
		LadspaRescan();
		Pool.CreateMenu();
		Pool.UpdateMenu();
		Pool.UpdateToolBar();
		break;

	case EXE_FILTER:							// apply filter, this is send by the filter or repeat function
		if (message->FindPointer("filter", (void**)&filter) == B_OK)
			ExecuteFilter(filter);
		break;
	
	case CANCEL_FILTER:							// apply filter, this is send by the filter or repeat function
		if (message->FindPointer("filter", (void**)&filter) == B_OK)
			CancelFilter(filter);
		break;
	
	case RUN_LAST_FILTER:							// run a filter with or without GUI
		RunLastFilter();
		break;
	
	case TRIM:
		DoTrim();
		break;
	
	case ZERO_IN:
		ZeroLR();
		ZeroRL();
		Pool.RedrawWindow();
		break;
	case ZERO_OUT:
		ZeroLL();
		ZeroRR();
		Pool.RedrawWindow();
		break;
	case ZERO_LL:
		ZeroLL();
		Pool.RedrawWindow();
		break;
	case ZERO_LR:
		ZeroLR();
		Pool.RedrawWindow();
		break;
	case ZERO_RL:
		ZeroRL();
		Pool.RedrawWindow();
		break;
	case ZERO_RR:
		ZeroRR();
		Pool.RedrawWindow();
		break;
	
	case SET_TIME:
		message->FindInt32("time", &x);
		Prefs.display_time = x;
		Pool.RedrawWindow();
		Pool.PrefWin->PostMessage(CHANGE_LANGUAGE);
		break;

	case B_KEY_DOWN:
//		message->PrintToStream();
		message->FindInt32("key", &raw_key);
		message->FindInt32("modifiers", &mod);
		message->FindInt32("raw_char", &key);

		// now do some conversions for combinations
		if (key == B_FUNCTION_KEY){
			key = 12+raw_key;
		}else if (key>='a' && key<='z')
			key -= ('a'-'A');
		
		msg = KeyBind.GetMessage(key, mod);
		if (msg){
			BMessage new_message(msg);
			new_message.AddString("language_key", KeyBind.GetID(key, mod));
			PostMessage(&new_message);
		}
		break;

	case B_MIME_DATA:			// let the app parse drops
	case B_SIMPLE_DATA:
		if (message->HasRef("refs")){
			HandleDrop(message);
			break;
		}
		be_app->PostMessage(message);
//		message->PrintToStream();
		break;				

	default:
//		message->PrintToStream();
		BWindow::MessageReceived(message);
	}
	
	// после каждого сообщения фокус снимается с полей ввода, но не с поля названия дорожки
	// (его принимает Enter или клик в другом месте): иначе оно закрывалось через секунду после клика
	if (CurrentFocus() && strcmp(CurrentFocus()->Name(), "nameedit") != 0)
		CurrentFocus()->MakeFocus(false);
}
