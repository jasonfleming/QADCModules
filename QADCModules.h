/*-------------------------------GPL-------------------------------------//
//
// QADCModules - A library for working with ADCIRC models
// Copyright (C) 2016  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
/**
 *
 *
 * \defgroup QADCModules Routines for use with the ADCIRC model
 *
 *
 * \class QADCModules
 *
 * \addtogroup QADCModules
 *
 * \brief A Qt C++ package for use with the ADCIRC model
 *
 * This class provides basic functions for working with
 * ADCIRC model data. (http://www.adcirc.org) It is written
 * in Qt C++ and has been tested with Qt version 5.5. The package
 * is provided as a dynamicly linked library (Windows)
 * or shared object file (Unix)
 *
 * \author Zachary Cobell
 * \version Version 0.1
 * \date 02/21/2016
 *
 * Contact: zcobell@gmail.com
 *
 * Created on: 02/21/2016
 *
 */
#ifndef QADCMODULES_H
#define QADCMODULES_H

#include "QADCModules_global.h"
#include <QObject>
#include <QVector>
#include <QFile>
#include <QMap>

class QADCMODULESSHARED_EXPORT QADCModules : public QObject
{
    Q_OBJECT
public:

    /** Initializes the class. Takes a QObject as a
     * parent parameter to allow Qt to handle the
     * memory management internally
     * @param parent [in] The QObject reference for memory management
     **/
    explicit QADCModules(QObject *parent = 0);

    ~QADCModules();

    //...Public Functions
    QString getErrorString(int error);

private:

    //...Private Variables
    int errorLevel;  ///
    QMap<int,QString> errorMapping;


    int initialize_errors();


};

#endif // QARCADISUTIL_H
