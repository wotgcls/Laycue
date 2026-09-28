#include "ui/pages/page-frame.hpp"

#include "ui/tokens.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace laycue::ui {

PageFrame::PageFrame(const QString &backLabel, const QString &title, QWidget *parent) : QWidget(parent)
{
	root_ = new QVBoxLayout(this);
	root_->setContentsMargins(0, 0, 0, 0);
	root_->setSpacing(size::gapSmall);

	auto *header = new QHBoxLayout();
	header->setSpacing(size::gapSmall);
	auto *back = new QPushButton(QStringLiteral("‹  ") + backLabel, this);
	back->setObjectName("laycueLink");
	back->setProperty("role", "control");
	back->setCursor(Qt::PointingHandCursor);
	back->setAccessibleName(backLabel);
	connect(back, &QPushButton::clicked, this, &PageFrame::backRequested);
	header->addWidget(back);
	header->addStretch(1);
	root_->addLayout(header);

	title_ = new QLabel(title, this);
	title_->setFont(uiFont(22, 700));
	title_->setStyleSheet(labelStyle(color::cream, title_->font()));
	title_->setContentsMargins(size::shadowMargin, 0, 0, 0);
	root_->addWidget(title_);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *host = new QWidget(scroll);
	body_ = new QVBoxLayout(host);
	body_->setContentsMargins(0, 0, 0, size::gap);
	body_->setSpacing(size::gap);
	body_->setAlignment(Qt::AlignTop);
	scroll->setWidget(host);
	root_->addWidget(scroll, 1);
}

void PageFrame::setFooter(QWidget *footer)
{
	root_->addWidget(footer);
}

void PageFrame::setTitle(const QString &title)
{
	title_->setText(title);
}

} // namespace laycue::ui
