#include "Flopster.h"
#include "GUI.h"

#include "resource.h"



extern void* hInstance;

inline HINSTANCE GetInstance() { return (HINSTANCE)hInstance; }



const char* GUI::GetClassName(void)
{
	sprintf(ClassName, "FLPS%08x", (unsigned long)(LONG_PTR)this);

	return (const char*)ClassName;
}



GUI::GUI(AudioEffect* effect)
{
	WNDCLASS wc;

	synth=(Flopster*)effect;

	wc.style=CS_GLOBALCLASS;
	wc.lpfnWndProc=(WNDPROC)WndProc;
	wc.cbClsExtra=0;
	wc.cbWndExtra=0;
	wc.hInstance=GetInstance();
	wc.hIcon=0;
	wc.hCursor=LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
	wc.lpszMenuName=0;
	wc.lpszClassName= GetClassName();

	RegisterClass(&wc);
}



GUI::~GUI()
{
	UnregisterClass(GetClassName(),GetInstance());
}
	
	
	
bool GUI::open(void* ptr)
{
	HWND parent;
	HINSTANCE hinst;
	BITMAP bm;
	VstInt32 sx,sy;

	AEffEditor::open(ptr);

	parent=HWND(ptr);
	hinst=GetInstance();

	//load background and char images from resource

	hBitmapBG = (HBITMAP)LoadImage(GetInstance(), MAKEINTRESOURCE(IDI_BG_IMAGE), IMAGE_BITMAP, 0, 0, 0);
	hBitmapCHR = (HBITMAP)LoadImage(GetInstance(), MAKEINTRESOURCE(IDI_CHR_IMAGE), IMAGE_BITMAP, 0, 0, 0);
	
	GetObject(hBitmapBG,sizeof(bm),&bm);

	guiWidth = bm.bmWidth;
	guiHeight = bm.bmHeight;

	GetObject(hBitmapCHR, sizeof(bm), &bm);

	charWdt = bm.bmWidth / 19;
	charHgt = bm.bmHeight;

	//create image of the same size for double buffering
	//it needs to have the same format and palette, so just copy the background

	hBitmapBuf=(HBITMAP)CopyImage(hBitmapBG,IMAGE_BITMAP,0,0,0);

	//get window size from the background image and create window

	rect.left  =0;
	rect.top   =0;
	rect.right =guiWidth;
	rect.bottom=guiHeight;

	hWndForm=CreateWindow(GetClassName(),"Flopster",WS_CHILD|WS_VISIBLE,0,0,(rect.right-rect.left),(rect.bottom-rect.top),parent,0,hinst,this);

	SetWindowLongPtr(hWndForm,GWLP_USERDATA,(LONG_PTR)this);

	//create all controls

	guiHover=-1;
	guiFocus=-1;
	guiFocusOriginX=0;
	guiFocusOriginY=0;

	guiControlCount=0;

	sx = 29;
	sy = 1;

	SliderAdd(sx, sy+0, pIdHeadStepGain);
	SliderAdd(sx, sy+1, pIdHeadSeekGain);
	SliderAdd(sx, sy+2, pIdHeadBuzzGain);
	SliderAdd(sx, sy+3, pIdSpindleGain);
	SliderAdd(sx, sy+4, pIdNoisesGain);
	SliderAdd(sx, sy+6, pIdDetune);
	SliderAdd(sx, sy+7, pIdOctaveShift);
	SliderAdd(sx, sy+9, pIdOutputGain);

	RenderActive = false;
	NeedUpdate = true;

	synth->UpdateGUI();

	return true;
}



void GUI::close()
{
	DestroyWindow(hWndForm);

	DeleteObject(hBitmapBuf);
	DeleteObject(hBitmapBG);
	DeleteObject(hBitmapCHR);

	AEffEditor::close();
}



bool GUI::getRect(ERect **ppErect)
{
   *ppErect=&rect;

   return true;
}



void GUI::idle(void)
{
	if (NeedUpdate)
	{
		NeedUpdate = false;
		InvalidateRect(hWndForm, NULL, false);
	}
}



//steps 0 no steps, 1..N steps, -1 zero at the middle

void GUI::SliderAdd(VstInt32 x,VstInt32 y,VstInt32 param)
{
	GUIControlStruct *s;

	s = &guiControlList[guiControlCount++];

	s->type = GUI_CTL_SLIDER;
	s->x = x * charWdt;
	s->y = y * charHgt;
	s->w = charWdt * 5;
	s->h = charHgt;
	s->param = param;
}



void GUI::SliderSet(GUIControlStruct *s,float value)
{
	if(value<0.0f) value=0.0f;
	if(value>1.0f) value=1.0f;

	synth->setParameter(s->param,value);
} 



float GUI::SliderGet(GUIControlStruct *s)
{
	float value;

	value=synth->getParameter(s->param);

	return value;
}



void GUI::SliderRender(GUIControlStruct *s, HDC hdc, bool hover, bool focus)
{
	char buf[16], val[3];
	VstInt32 n;
	switch(s->type)
	{
	case GUI_CTL_SLIDER:
		{
		synth->getParameterDisplay(s->param, buf);

		val[0] = 0;
		val[1] = 0;
		val[2] = 0;

		n = abs(atoi(buf));

		if (n >= 100) val[0] = (n / 100 % 10) + 6;
		if (n >= 10) val[1] = (n / 10 % 10) + 6;
		val[2] = (n % 10) + 6;

		if (buf[0] == '-') val[0] = 5;

		RenderChar(hdc, s->x + 0 * charWdt, s->y, (hover | focus) ? 3 : 1);
		RenderChar(hdc, s->x + 1 * charWdt, s->y, val[0]);
		RenderChar(hdc, s->x + 2 * charWdt, s->y, val[1]);
		RenderChar(hdc, s->x + 3 * charWdt, s->y, val[2]);
		RenderChar(hdc, s->x + 4 * charWdt, s->y, (hover | focus) ? 4 : 2);
		}
		break;
	}
}



VstInt32 GUI::ControlCheckArea(GUIControlStruct *s,VstInt32 mx,VstInt32 my)
{
	if (mx < s->x || mx >= s->x + s->w || my < s->y || my >= s->y + s->h) return FALSE;

	return TRUE;
}



void GUI::SliderCheckChange(GUIControlStruct *s,VstInt32 mx,VstInt32 my,VstInt32 click)
{
	float value;

	if(s->type!=GUI_CTL_SLIDER) return;

	switch(click)
	{
	case GUI_BTN_L_DOWN:
		{
			value=SliderGet(s);

			value += (float)(guiFocusOriginY - my) / 250.0f;

			guiFocusOriginY=my;

			SliderSet(s,value);
		}
		break;

	case GUI_BTN_R_DOWN:
		{
			SliderSet(s,.5f);
		}
		break;
	}
}



void GUI::ProcessMouse(UINT message,WPARAM wParam,LPARAM lParam)
{
	VstInt32 i,mx,my,click;
	GUIControlStruct *s;

	click=0;

	mx=GET_X_LPARAM(lParam); 
	my=GET_Y_LPARAM(lParam); 

	if(message==WM_LBUTTONDOWN||message==WM_MOUSEMOVE) if(wParam&MK_LBUTTON) click=GUI_BTN_L_DOWN;
	if(message==WM_RBUTTONDOWN||message==WM_MOUSEMOVE) if(wParam&MK_RBUTTON) click=GUI_BTN_R_DOWN;
	if(message==WM_LBUTTONUP) click=GUI_BTN_L_UP;
	if(message==WM_RBUTTONUP) click=GUI_BTN_R_UP;

	guiHover=-1;

	for(i=0;i<guiControlCount;++i)
	{
		s=&guiControlList[i];

		if(ControlCheckArea(s,mx,my))
		{
			if(guiFocus<0)
			{
				guiHover=i;
				guiHoverX=mx;
				guiHoverY=my;
			}

			if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN)
			{
				guiFocus=guiHover;
				guiFocusOriginX=mx;
				guiFocusOriginY=my;
			}
		}
	}

	if(guiFocus>=0)
	{
		SliderCheckChange(&guiControlList[guiFocus], mx, my, click);
	}

	if(message==WM_LBUTTONUP||message==WM_RBUTTONUP)
	{
		guiFocus=-1;
	}
}



void GUI::Update(void)
{
	NeedUpdate = true;
}



void GUI::ShowError(const char* error)
{
	MessageBox(hWndForm, error, "Flopster VST encounter an issue", MB_OK);
}



void GUI::RenderChar(HDC hdcDst, VstInt32 sx, VstInt32 sy, VstInt32 chr)
{
	HDC hdcSrc;

	hdcSrc = CreateCompatibleDC(NULL);

	SelectObject(hdcSrc, hBitmapCHR);

	BitBlt(hdcDst, sx, sy, charWdt, charHgt, hdcSrc, chr*charWdt, 0, SRCCOPY);

	DeleteDC(hdcSrc);
}



void GUI::RenderControls(HDC hdc)
{
	GUIControlStruct *s;
	VstInt32 i;

	for (i = 0; i < guiControlCount; ++i)
	{
		s=&guiControlList[i];

		switch(s->type)
		{
		case GUI_CTL_SLIDER:
			SliderRender(s, hdc, (i == guiHover) ? true : false, (i == guiFocus) ? true : false);
			break;
		}
	}
}



void GUI::RenderHead(HDC hdc, VstInt32 sx, VstInt32 sy, VstInt32 w, VstInt32 h, VstInt32 pos)
{
	VstInt32 hh, hy;

	hh = 16;
	hy = sy + ((h - hh) / 80 * pos);

	SelectObject(hdc, GetStockObject(DC_PEN));
	SelectObject(hdc, GetStockObject(DC_BRUSH));

	SetDCBrushColor(hdc, 0x000000);
	SetDCPenColor(hdc, 0x000000);

	Rectangle(hdc, sx, sy, sx + w, hy);
	Rectangle(hdc, sx, hy + hh, sx + w, sy + h);
}



void GUI::RenderAll(void)
{
	HDC hdcWnd,hdcSrc,hdcDst;
	PAINTSTRUCT ps;
	BITMAP bm;
	VstInt32 pos;
	
	if(RenderActive) return;

	RenderActive=true;

	//copy the background image to the buffer

    hdcSrc=CreateCompatibleDC(NULL);
    hdcDst=CreateCompatibleDC(NULL);

    SelectObject(hdcSrc,hBitmapBG);
    SelectObject(hdcDst,hBitmapBuf);

    GetObject(hBitmapBG,sizeof(bm),&bm);

    BitBlt(hdcDst,0,0,bm.bmWidth,bm.bmHeight,hdcSrc,0,0,SRCCOPY);

    DeleteDC(hdcSrc);
    
	RenderControls(hdcDst);

	RenderChar(hdcDst, 27 * charWdt, 1 * charHgt, (synth->FDD.sample_type == SAMPLE_TYPE_STEP) ? 16 : 17);
	RenderChar(hdcDst, 27 * charWdt, 2 * charHgt, (!synth->FDD.head_sample_loop_done && synth->FDD.sample_type == SAMPLE_TYPE_SEEK) ? 16 : 17);
	RenderChar(hdcDst, 27 * charWdt, 3 * charHgt, (!synth->FDD.head_sample_loop_done && synth->FDD.sample_type == SAMPLE_TYPE_BUZZ) ? 16 : 17);
	RenderChar(hdcDst, 27 * charWdt, 4 * charHgt, synth->FDD.spindle_enable ? 16 : 17);
	RenderChar(hdcDst, 27 * charWdt, 5 * charHgt, (synth->FDD.sample_type == SAMPLE_TYPE_NOISE) ? 16 : 17);

	pos = synth->FDD.head_pos;

	if (pos >= 80) pos = 159 - pos;

	RenderHead(hdcDst, 10 * charWdt + charWdt / 2, 7 * charHgt + charHgt / 4, charWdt * 2, charHgt * 3, pos);

	DeleteDC(hdcDst);

	//draw the buffer to the form

	hdcWnd=BeginPaint(hWndForm,&ps);

	hdcSrc=CreateCompatibleDC(hdcWnd);

	SelectObject(hdcSrc,hBitmapBuf);

	BitBlt(hdcWnd,0,0,guiWidth,guiHeight,hdcSrc,0,0,SRCCOPY);

	DeleteDC(hdcSrc);

	EndPaint(hWndForm,&ps);

	RenderActive=false;
}



LRESULT WINAPI GUI::WndProc(HWND hWnd,UINT message,WPARAM wParam,LPARAM lParam)
{
	GUI *gui;

	gui=(GUI*)GetWindowLongPtr(hWnd,GWLP_USERDATA);

	switch(message)
	{
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MOUSEMOVE:
		{
			if(message==WM_LBUTTONDOWN) SetCapture(hWnd);
			if(message==WM_LBUTTONUP) ReleaseCapture();

			gui->ProcessMouse(message,wParam,lParam);
			
			InvalidateRect(hWnd,NULL,false);
		}
		return 0;

	case WM_PAINT:
		{
			gui->RenderAll();
		}
		return 0;
	}

	return DefWindowProc(hWnd,message,wParam,lParam);
}