#include "backend.h"
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMimeData>

Backend::Backend(QObject* parent) : QObject(parent), settings_("PocketDrop", "PocketDrop") {
    ui_ = std::make_unique<Ui>(*this);
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &Backend::tick);
}

Backend::~Backend() { shutdown(); }

void Backend::start() {
    if (started_) return;
    started_ = true;
    ui_->start();
    timer_.start();
    refresh();
}

void Backend::shutdown() {
    if (!started_) return;
    started_ = false;
    timer_.stop();
    ui_->shutdown();
}

void Backend::tick() {
    ui_->tick();
    refresh();
}

// The engine's whole picture as JSON; rows get the key the view uses to remove them.
void Backend::refresh() {
    refreshQueued_ = false;
    if (!started_) return;
    QString json = QString::fromStdString(ui_->stateJson());
    if (json == last_) return;
    last_ = json;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
    st_ = doc.object().toVariantMap();
    rows_.clear();
    for (const QJsonValue& v : doc.object().value("rows").toArray()) {
        QVariantMap row = v.toObject().toVariantMap();
        row["key"] = "e" + QString::number(row.value("index").toInt());
        row["group"] = QString();
        row["thumbs"] = QVariantList();
        rows_.push_back(row);
    }
    emit stateChanged();
    emit rowsChanged();
}

void Backend::command(const QString& name, const QString& arg) {
    ui_->command(name.toStdString(), arg.toStdString());
    refresh();
}

// Anywhere asks first, in the view's own dialog, unless the person said not to.
void Backend::setMode(int mode) {
    if (mode == 1 && st_.value("mode").toInt() != 1 && !settings_.value("AnywhereRiskDismissed", 0).toInt()) {
        risk_ = true;
        emit riskChanged();
        return;
    }
    command("mode", QString::number(mode));
}

void Backend::confirmRisk(bool proceed, bool dontAskAgain) {
    risk_ = false;
    emit riskChanged();
    if (!proceed) return;
    riskOk_ = true;
    riskDismiss_ = dontAskAgain;
    command("mode", "1");
    riskOk_ = false;
}

bool Backend::confirmAnywhereRisk(bool& dontShowAgain) {
    dontShowAgain = riskOk_ && riskDismiss_;
    return riskOk_;
}

void Backend::addPaths(const QStringList& paths) {
    std::vector<std::string> list;
    for (const QString& p : paths) list.push_back(p.toStdString());
    if (!list.empty()) ui_->addPaths(list);
    refresh();
}

void Backend::addUrls(const QVariantList& urls) {
    QStringList paths;
    for (const QVariant& v : urls) {
        QUrl url = v.metaType().id() == QMetaType::QUrl ? v.toUrl() : QUrl(v.toString());
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    addPaths(paths);
}

void Backend::addText(const QString& text) {
    ui_->addText(text.toStdString());
    refresh();
}

void Backend::removeRow(const QString& key) {
    if (key.startsWith('e')) command("remove", key.mid(1));
}

void Backend::clearAll() { command("clear"); }

void Backend::changeInbox(const QUrl& folder) {
    if (folder.isLocalFile()) command("set_inbox", QDir::toNativeSeparators(folder.toLocalFile()));
}

// ---- Shell -------------------------------------------------------------------------------------------------------------------
void Backend::invalidate() {
    if (refreshQueued_) return;
    refreshQueued_ = true;
    QMetaObject::invokeMethod(this, &Backend::refresh, Qt::QueuedConnection);
}

void Backend::setAnimating(bool on) { timer_.setInterval(on ? 33 : 500); }

void Backend::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

void Backend::copyText(const std::string& text) { QGuiApplication::clipboard()->setText(QString::fromStdString(text)); }

bool Backend::copyImage(const std::vector<uint8_t>& rgba, int w, int h) {
    if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * (size_t)h * 4) return false;
    QImage image(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888);
    QGuiApplication::clipboard()->setImage(image.copy());
    return true;
}

void Backend::readClipboard(std::vector<std::string>& paths, std::string& text) {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return;
    if (mime->hasUrls()) {
        for (const QUrl& u : mime->urls())
            if (u.isLocalFile()) paths.push_back(u.toLocalFile().toStdString());
        if (!paths.empty()) return;
    }
    if (mime->hasImage()) {
        QImage image = qvariant_cast<QImage>(mime->imageData());
        QDir dir(QString::fromStdString(plat::app_data_dir()) + "/Pasted");
        dir.mkpath(".");
        QString file = dir.filePath("PocketDrop-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".png");
        if (!image.isNull() && image.save(file, "PNG")) {
            paths.push_back(file.toStdString());
            return;
        }
    }
    if (mime->hasText()) text = mime->text().toStdString();
}

// Choosing files and folders is the view's own dialogs: the request goes up as a signal and the answer comes back as addUrls.
void Backend::browse(bool folders, std::function<void(const std::vector<std::string>&)>) { emit browseRequested(folders); }

void Backend::chooseFolder(const std::string&, std::function<void(const std::string&)>) { emit chooseInboxRequested(); }

void Backend::openUrl(const std::string& url) {
    QString text = QString::fromStdString(url);
    QUrl target(text);
    if (target.scheme().isEmpty()) target = QUrl::fromLocalFile(text);
    QDesktopServices::openUrl(target);
}

void Backend::revealPath(const std::string& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(QString::fromStdString(path)).absolutePath()));
}

void Backend::alert(const std::string& title, const std::string& message) {
    emit alertRequested(QString::fromStdString(title), QString::fromStdString(message));
}

int Backend::loadSetting(const char* key, int def) { return settings_.value(key, def).toInt(); }
void Backend::saveSetting(const char* key, int value) { settings_.setValue(key, value); }
std::string Backend::loadString(const char* key, const std::string& def) {
    return settings_.value(key, QString::fromStdString(def)).toString().toStdString();
}
void Backend::saveString(const char* key, const std::string& value) { settings_.setValue(key, QString::fromStdString(value)); }

std::string Backend::cleanPath(const std::string& path) {
    QFileInfo info(QString::fromStdString(path));
    if (!info.exists()) return {};
    return QDir::toNativeSeparators(info.absoluteFilePath()).toStdString();
}
