#pragma once
// The standalone app's backend: PocketDrop's engine (src/ui/ui.h) behind the contract qml/PocketDropView.qml reads.
// It implements the engine's Shell with Qt, runs its clock, and turns Ui::stateJson() into the `st` and `rows` properties.
#include "../src/ui/shell.h"
#include "../src/ui/ui.h"
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class Backend : public QObject, public Shell {
    Q_OBJECT
    Q_PROPERTY(QVariantMap st READ st NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(bool riskPending READ riskPending NOTIFY riskChanged)

public:
    explicit Backend(QObject* parent = nullptr);
    ~Backend() override;

    QVariantMap st() const { return st_; }
    QVariantList rows() const { return rows_; }
    bool riskPending() const { return risk_; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void command(const QString& name, const QString& arg = {});
    Q_INVOKABLE void setMode(int mode);
    Q_INVOKABLE void confirmRisk(bool proceed, bool dontAskAgain);
    Q_INVOKABLE void addUrls(const QVariantList& urls);
    Q_INVOKABLE void addText(const QString& text);
    Q_INVOKABLE void removeRow(const QString& key);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void changeInbox(const QUrl& folder);
    void addPaths(const QStringList& paths);

signals:
    void stateChanged();
    void rowsChanged();
    void riskChanged();
    void browseRequested(bool folders);
    void chooseInboxRequested();
    void alertRequested(const QString& title, const QString& message);
    void topmostRequested(bool on);
    void attentionRequested();

    // ---- Shell ----
public:
    void invalidate() override;
    void setAnimating(bool on) override;
    void post(std::function<void()> fn) override;
    void clientSize(float& w, float& h) override { w = 385; h = 750; }
    void copyText(const std::string& text) override;
    bool copyImage(const std::vector<uint8_t>& rgba, int w, int h) override;
    void readClipboard(std::vector<std::string>& paths, std::string& text) override;
    void browse(bool folders, std::function<void(const std::vector<std::string>&)> done) override;
    void chooseFolder(const std::string& title, std::function<void(const std::string&)> done) override;
    void openUrl(const std::string& url) override;
    bool confirmAnywhereRisk(bool& dontShowAgain) override;
    void revealPath(const std::string& path) override;
    void attention() override { emit attentionRequested(); }
    int popupMenu(const std::vector<MenuItem>&, float, float) override { return 0; } // menus are drawn by the QML
    void alert(const std::string& title, const std::string& message) override;
    int loadSetting(const char* key, int def) override;
    void saveSetting(const char* key, int value) override;
    std::string loadString(const char* key, const std::string& def) override;
    void saveString(const char* key, const std::string& value) override;
    void setTopmost(bool on) override { emit topmostRequested(on); }
    std::string cleanPath(const std::string& path) override;

private:
    void refresh();
    void tick();

    QSettings settings_;
    std::unique_ptr<Ui> ui_;
    QTimer timer_;
    QVariantMap st_;
    QVariantList rows_;
    QString last_;
    bool started_ = false, risk_ = false, riskOk_ = false, riskDismiss_ = false, refreshQueued_ = false;
};
