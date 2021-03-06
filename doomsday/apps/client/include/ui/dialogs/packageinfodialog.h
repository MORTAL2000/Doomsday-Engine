/** @file packagepopupwidget.h  Popup showing information and actions about a package.
 *
 * @authors Copyright (c) 2016-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
 *
 * @par License
 * GPL: http://www.gnu.org/licenses/gpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the GNU
 * General Public License along with this program; if not, see:
 * http://www.gnu.org/licenses</small>
 */

#ifndef DENG_CLIENT_UI_PACKAGEINFODIALOG_H
#define DENG_CLIENT_UI_PACKAGEINFODIALOG_H

#include <de/DialogWidget>

/**
 * Dialog showing information about a package.
 */
class PackageInfoDialog : public de::DialogWidget
{
    Q_OBJECT

public:
    enum Mode { EnableActions, InformationOnly };

public:
    PackageInfoDialog(de::String const &packageId, Mode mode);
    PackageInfoDialog(de::File const *packageFile, Mode mode);

protected:
    void prepare() override;

public slots:
    void playInGame();
    void addToProfile();
    void configure();
    void showFile();

private:
    DENG2_PRIVATE(d)
};

#endif // DENG_CLIENT_UI_PACKAGEINFODIALOG_H
