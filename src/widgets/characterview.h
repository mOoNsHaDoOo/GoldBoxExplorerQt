#pragma once

#include "parse/character.h"

#include <QTabWidget>

class CharacterView : public QTabWidget
{
    Q_OBJECT
public:
    explicit CharacterView(CharacterDocument document, QWidget *parent = nullptr);
};
