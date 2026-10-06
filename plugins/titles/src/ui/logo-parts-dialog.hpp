// "Logo parts": the operator drags a line across their logo picture to say where the logo mark ends and the
// wordmark begins (side by side or stacked), and which part is the mark.
#pragma once

#include <QDialog>
#include <QImage>

namespace laycue::ui {
class Segmented;
}

namespace laycue::titles::ui {

class PartsView;

class LogoPartsDialog : public QDialog {
	Q_OBJECT

public:
	LogoPartsDialog(const QString &logoPath, int cut, bool stacked, bool markSecond, QWidget *parent = nullptr);

	// 0 when the operator chose to keep the logo as one piece.
	int cut() const { return cut_; }
	bool stacked() const;
	bool markSecond() const;

private:
	void refreshChoices();

	PartsView *view_ = nullptr;
	laycue::ui::Segmented *orientation_ = nullptr;
	laycue::ui::Segmented *which_ = nullptr;
	int cut_ = 0;
};

} // namespace laycue::titles::ui
