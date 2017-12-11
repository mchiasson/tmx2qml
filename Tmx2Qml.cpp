/*******************************************************************************
 * MIT License
 *
 * Copyright (c) 2017 Mathieu-André Chiasson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

#include "Tmx2Qml.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>

// libtiled
#include <mapreader.h>
#include <map.h>
#include <tilelayer.h>

Tmx2Qml::Tmx2Qml(QObject *parent) :
    QObject(parent),
    m_utc(QDateTime::currentDateTime())
{
    m_utc.setTimeSpec(Qt::UTC);
    QObject::connect(this, &Tmx2Qml::launch,
                     this, &Tmx2Qml::onLaunched,
                     Qt::QueuedConnection);

}

void Tmx2Qml::onLaunched()
{
    QCoreApplication *app = QCoreApplication::instance();

    if (app->arguments().size() != 2)
    {
        printUsage();
        app->exit(-1);
        return;
    }

    QString mapPath = app->arguments().at(1);

    Tiled::MapReader reader;
    Tiled::Map *map = reader.readMap(mapPath);
    if (map == nullptr)
    {
        qCritical() << qPrintable(mapPath) << "Error:" << qPrintable(reader.errorString());
        app->exit(-1);
        return;
    }

    QString mapPrefix = mapPath.split("/").last().split(".").at(0);
    mapPrefix.replace(0, 1, QString(mapPrefix[0]).toUpper());

    exportMap(mapPrefix, map);


    delete map;

    app->exit(0);
}

void Tmx2Qml::exportMap(QString mapPrefix, Tiled::Map *map)
{
    QSet<const Tiled::Tile*> uniqueTiles;
    QMap<Tiled::Tileset*, QVector< QVector<Tiled::Frame> > > animationMap;

    QFile file(QString("%1Map.qml").arg(mapPrefix));

    file.open(QFile::WriteOnly | QFile::Text);
    QTextStream out(&file);
    printHeader(out);
    out << "import QtQuick 2.0" << endl;
    out << endl;
    out << "Flickable {" << endl;
    out << "\tid: root" << endl;
    out << "\tcontentWidth: " << map->width()*map->tileWidth() << endl;
    out << "\tcontentHeight: " << map->height()*map->tileHeight() << endl;
    out << "\tboundsBehavior: Flickable.StopAtBounds" << endl;
    QList<Tiled::TileLayer*> layers = map->tileLayers();
    for (int i = 0; i < layers.size(); ++i)
    {
        Tiled::TileLayer *layer = layers.at(i);
        QString layerId = layer->name().toLower().replace(" ", "_");
        out << "\tproperty alias " << layerId << ": " << layerId  << endl;;
    }

    for (int i = 0; i < layers.size(); ++i)
    {
        Tiled::TileLayer *layer = layers.at(i);
        QString layerId = layer->name().toLower().replace(" ", "_");
        out << "\tItem {" << endl;
        out << "\t\tid: " << layerId << endl;
        if (!qFuzzyCompare(layer->offset().x(), 0.0))
        {
            out << "\t\tx: " << layer->offset().x() << endl;
        }
        if (!qFuzzyCompare(layer->offset().y(), 0.0))
        {
            out << "\t\ty: " << layer->offset().y() << endl;
        }
        if (!qFuzzyCompare(layer->opacity(), 1.0f))
        {
            out << "\t\topacity: " << layer->opacity() << endl;
        }
        if (!layer->isVisible())
        {
            out << "\t\tvisible: false" << endl;
        }

        for (int y = 0; y < layer->height(); ++y)
        {
            for (int x = 0; x < layer->width(); ++x)
            {
                const Tiled::Cell &cell = layer->cellAt(x, y);
                Tiled::Tileset *tileset = cell.tileset();
                if (tileset)
                {
                    QVector< QVector<Tiled::Frame> > &animations = animationMap[tileset];

                    const Tiled::Tile *tile = tileset->tileAt(cell.tileId());
                    if (tile)
                    {
                        if (tile->isAnimated())
                        {
                            const QVector<Tiled::Frame> &frames = tile->frames();
                            bool newFrames = true;
                            for(int i = 0; i < animations.size(); ++i)
                            {
                                if (frames == animations[i])
                                {
                                    newFrames = false;
                                    break;
                                }
                            }

                            if (newFrames)
                            {
                                animations.push_back(frames);
                            }

                            // generate a unique animation ID
                            QString id = tileset->name().toLower();
                            id.replace(" ", "_");
                            for(int i = 0; i < frames.size(); ++i)
                            {
                                const Tiled::Frame &frame = frames[i];
                                uniqueTiles << tileset->tileAt(frame.tileId);
                                id += QString("_%1").arg(frame.tileId);
                            }

                            out << "\t\tImage{x:" << x*map->tileWidth() << ";y:" << y*map->tileHeight() << ";source:" << id << "}" << endl;
                        }
                        else
                        {
                            uniqueTiles << tile;
                            QString id = QString("%1_%2").arg(tileset->name().toLower()).arg(tile->id());
                            id.replace(" ", "_");
                            out << "\t\tImage{x:" << x*map->tileWidth() << ";y:" << y*map->tileHeight() << ";source:\"" << id << ".png\"}" << endl;
                        }
                    }
                }
            }
        }
        out << "\t}" << endl;
    }

    QMap<Tiled::Tileset*, QVector< QVector<Tiled::Frame> > >::const_iterator it = animationMap.begin(), end = animationMap.end();

    while (it != end)
    {
        Tiled::Tileset* tileset = it.key();
        const QVector< QVector<Tiled::Frame> > &animations = it.value();

        foreach(const QVector<Tiled::Frame> &animation, animations)
        {

            QString id_base = tileset->name().toLower();
            id_base.replace(" ", "_");

            QString id = id_base;
            foreach(const Tiled::Frame &frame, animation)
            {
                id += QString("_%1").arg(frame.tileId);
            }

            out << "\tproperty string " << id << ": \"\"" << endl;
            out << "\tSequentialAnimation{" << endl;
            out << "\t\trunning:true" << endl;
            out << "\t\tloops: Animation.Infinite" << endl;
            foreach(const Tiled::Frame &frame, animation)
            {
                out << "\t\tScriptAction{script:" <<id << "=\"" << id_base << "_" << frame.tileId << ".png\"}" << endl;
                out << "\t\tPauseAnimation{duration:" << frame.duration << "}" << endl;
            }
            out << "\t}" << endl;

        }

        ++it;
    }

    out << "}" << endl;;


    QList<QString> assets;

    assets << QString("%1Map.qml").arg(mapPrefix);

    foreach (const Tiled::Tile *tile, uniqueTiles) {
        const QPixmap &image = tile->image();
        QString imageAssetName = QString("%1_%2.png").arg(tile->tileset()->name().toLower()).arg(tile->id());
        imageAssetName.replace(" ", "_");
        assets << imageAssetName;
        QFile file(imageAssetName);
        file.open(QIODevice::WriteOnly);
        image.save(&file, "PNG");
    }

    std::sort(assets.begin(), assets.end(), std::less<QString>());

    exportQRC(mapPrefix, assets);
}

void Tmx2Qml::exportQRC(QString mapPrefix, QList<QString> assets)
{
    QFile file(QString("%1Map.qrc").arg(mapPrefix));
    if (file.open(QFile::WriteOnly | QFile::Text))
    {
        QTextStream out(&file);

        out << "<!--" << endl << endl;
        printHeader(out);
        out << "-->" << endl;
        out << "<RCC>" << endl;
        out << "    <qresource prefix=\"/\">" << endl;

        foreach(const QString asset, assets)
        {
            out << "        <file>" << asset << "</file>" << endl;
        }

        out << "    </qresource>" << endl;
        out << "</RCC>" << endl;
    }

}

void Tmx2Qml::printUsage()
{
    qInfo() << "Usage :";
    qInfo() << "   " << qPrintable(QCoreApplication::applicationName()) << "<path-to-tmx-file>";
}

void Tmx2Qml::printHeader(QTextStream &out)
{
    out << "/*******************************************************************************" << endl;
    out << " * *** WARNING : DO NOT EDIT!!!" << endl;
    out << " * This file was generated by \"" << qPrintable(QCoreApplication::applicationName()) << "\" on " << qPrintable(m_utc.toString()) << endl;
    out << " *" << endl;
    out << " * For more information about " << qPrintable(QCoreApplication::applicationName()) << ", please visit" << endl;
    out << " *     https://github.com/mchiasson/" << qPrintable(QCoreApplication::applicationName()) << endl;
    out << " *" << endl;
    out << " * For more information about Tiled Map Editor, please visit" << endl;
    out << " *     http://www.mapeditor.org/" << endl;
    out << " * Don't forget to show your support the creator of Tiled Map Editor: " << endl;
    out << " *     https://www.patreon.com/bjorn" << endl;
    out << " ******************************************************************************/" << endl;
    out << endl;
}

