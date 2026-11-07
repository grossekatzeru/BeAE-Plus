/*
 * ParamSlider - бегунок параметра с подписью и полем ввода рядом: значение можно задать точно, вписав число.
 * Основа для LadspaSlider и VstSlider. Подкласс описывает, как позиция бегунка связана с текстом.
 * Сообщение, переданное в конструктор, уходит при любом изменении (бегунком или вводом).
 */
#ifndef PARAM_SLIDER_H
#define PARAM_SLIDER_H

#include <Control.h>
#include <String.h>

class BSlider;
class BStringView;
class BTextControl;

class ParamSlider : public BControl {
public:
	ParamSlider(const char *label, BMessage *message, int32 minPos, int32 maxPos);
	virtual ~ParamSlider();

	virtual void AttachedToWindow();
	virtual void FrameResized(float w, float h);
	virtual void MessageReceived(BMessage *msg);
	virtual void SetEnabled(bool enabled);

	int32	Pos() const;
	void	SetPosLimits(int32 minPos, int32 maxPos);
	void	SetPos(int32 pos);			// без сообщений
	void	RefreshText();				// обновить поле по текущему значению

protected:
	virtual BString	FormatText() const = 0;				// что показать в поле
	virtual bool	ApplyText(const char *text) = 0;	// ввод в поле; false - не число
	virtual void	SliderMoved() {}					// бегунок сдвинут рукой

private:
	void	Place();
	BStringView		*fLabel;
	BSlider			*fSlider;
	BTextControl	*fText;
};

#endif
