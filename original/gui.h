#define _GUI_ACTIVE_

#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <vector>

using namespace std;


#include "aeffeditor.h"



#define COL_FRAME	RGB(220,90,128)
#define COL_STEPS	RGB(255,140,180)
#define COL_HANDLE	RGB(255,255,255)
#define COL_ACTIVE	RGB(245,200,215)
#define COL_GRAPH1	RGB(245,200,215)
#define COL_GRAPH2	RGB(180,50,88)
#define COL_GRAPH3	RGB(240,110,150)
#define COL_WHITE	RGB(255,255,255)



enum {
	GUI_CTL_SLIDER=0
};



enum {

	GUI_BTN_NONE=0,
	GUI_BTN_L_DOWN,
	GUI_BTN_R_DOWN,
	GUI_BTN_L_UP,
	GUI_BTN_R_UP

};



#define GUI_HANDLE_HEIGHT	14

#define GUI_MAX_CONTROLS	64



struct GUIControlStruct {

	VstInt32 type;

	VstInt32 x, y;
	VstInt32 w, h;

	VstInt32 param;
	VstInt32 steps;

	bool hover;

};	



class GUI:public AEffEditor
{
private:

	Flopster *synth;
	
	char ClassName[32];

	HWND hWndForm;

	HBITMAP hBitmapBG;
	HBITMAP hBitmapCHR;
	HBITMAP hBitmapBuf;

	VstInt32 guiWidth;
	VstInt32 guiHeight;

	ERect rect;

	GUIControlStruct guiControlList[GUI_MAX_CONTROLS];

	VstInt32 guiControlCount;

	VstInt32 guiHover;
	VstInt32 guiHoverX;
	VstInt32 guiHoverY;

	VstInt32 guiFocus;
	VstInt32 guiFocusOriginX;
	VstInt32 guiFocusOriginY;

	VstInt32 charWdt;
	VstInt32 charHgt;

	const char* GetClassName(void);

	static LRESULT WINAPI WndProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam);

	void SliderAdd(VstInt32 x,VstInt32 y,VstInt32 param);
	void SliderSet(GUIControlStruct *s,float value);
	float SliderGet(GUIControlStruct *s);
	void SliderRender(GUIControlStruct *s,HDC hdc,bool hover,bool focus);

	VstInt32 ControlCheckArea(GUIControlStruct *s,VstInt32 mx,VstInt32 my);

	void SliderCheckChange(GUIControlStruct *s,VstInt32 mx,VstInt32 my,VstInt32 click);

	void ProcessMouse(UINT message,WPARAM wParam,LPARAM lParam);

	void RenderChar(HDC hdcDst, VstInt32 sx, VstInt32 sy, VstInt32 chr);
	void RenderControls(HDC hdc);
	void RenderHead(HDC hdc, VstInt32 sx, VstInt32 sy, VstInt32 w, VstInt32 h, VstInt32 pos);
	void RenderAll(void);

	bool RenderActive;
	bool NeedUpdate;

public:

	GUI(AudioEffect* effect);

	~GUI();

	virtual bool open(void* ptr);

	virtual void close();

	virtual void idle();

	virtual bool getRect(ERect **ppRect);

	void Update(void);
	void ShowError(const char* error);
};
