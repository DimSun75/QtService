﻿#include <QString>
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <basicservicetest.h>
#include <qt_windows.h>
using namespace QtService;

#ifdef QT_NO_DEBUG
#define LIB(x) QStringLiteral(x ".dll")
#else
#define LIB(x) QStringLiteral(x "d.dll")
#endif

class TestWindowsService : public BasicServiceTest
{
	Q_OBJECT

protected:
	QString backend() override;
	QString name() override;
	void init() override;
	void cleanup() override;
	void testCustomImpl() override;

private:
	QTemporaryDir _svcDir;
	SC_HANDLE _manager = nullptr;
	bool _printLog = false;
};

QString TestWindowsService::backend()
{
	return QStringLiteral("windows");
}

QString TestWindowsService::name()
{
	return QStringLiteral("testservice");
}

void TestWindowsService::init()
{
	QDir svcDir{_svcDir.path()};
	QVERIFY(svcDir.mkpath(QStringLiteral(".")));
	QVERIFY(svcDir.exists());

	// copy the primary executable
	const auto svcName = QStringLiteral("testservice.exe");
#ifdef QT_NO_DEBUG
	const QString svcSrcPath{QCoreApplication::applicationDirPath() + QStringLiteral("/../../TestService/release/") + svcName};
	const auto deployType = QStringLiteral("--release");
#else
	const QString svcSrcPath{QCoreApplication::applicationDirPath() + QStringLiteral("/../../TestService/debug/") + svcName};
	const auto deployType = QStringLiteral("--debug");
#endif
	QVERIFY(QFile::exists(svcSrcPath));
	QVERIFY(QFile::copy(svcSrcPath, svcDir.absoluteFilePath(svcName)));
	const auto svcArg = QStringLiteral("\"%1\" --backend windows").arg(QDir::toNativeSeparators(svcDir.absoluteFilePath(svcName)));

	// copy svc lib into host lib dir (required by windeployqt)
	const auto svcLib = LIB("Qt5Service");
	const QDir bLibDir{QCoreApplication::applicationDirPath() + QStringLiteral("/../../../../../lib")};
	QDir hLibDir{QStringLiteral(QT_LIB_DIR)};
	QVERIFY(bLibDir.exists(svcLib));
	hLibDir.remove(svcLib);
	QVERIFY(QFile::copy(bLibDir.absoluteFilePath(svcLib), hLibDir.absoluteFilePath(svcLib)));

	// run windeployqt
	QProcess windepProc;
	windepProc.setProgram(QStringLiteral("windeployqt.exe"));  // should be in path
	windepProc.setArguments(QStringList{
								deployType,
								QStringLiteral("--pdb"),
								QStringLiteral("--no-quick-import"),
								QStringLiteral("--no-translations"),
								QStringLiteral("--compiler-runtime"),
								svcName
							});
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("VCINSTALLDIR"), QStringLiteral("%1\\VC").arg(qEnvironmentVariable("VSINSTALLDIR")));
	windepProc.setProcessEnvironment(env);
	windepProc.setWorkingDirectory(svcDir.absolutePath());
	windepProc.setProcessChannelMode(QProcess::SeparateChannels);
	windepProc.setStandardOutputFile(QProcess::nullDevice());
	windepProc.start();
	QVERIFY2(windepProc.waitForFinished(), qUtf8Printable(windepProc.errorString()));
	qInfo() << "windeployqt errors:" << windepProc.readAllStandardError().constData();
	QVERIFY2(windepProc.exitStatus() == QProcess::NormalExit, qUtf8Printable(windepProc.errorString()));
	QCOMPARE(windepProc.exitCode(), EXIT_SUCCESS);

	// write qt.conf
	QFile qtConf {svcDir.absoluteFilePath(QStringLiteral("qt.conf"))};
	QVERIFY(qtConf.open(QIODevice::WriteOnly | QIODevice::Text));
	qtConf.write("[Paths]\n");
	qtConf.write("Prefix=.\n");
	qtConf.write("Binaries=.\n");
	qtConf.write("Libraries=.\n");
	qtConf.write("Plugins=.\n");
	qtConf.close();

	// add plugins to Qt
	const auto plgSubDir = QStringLiteral("servicebackends");
	QDir bPlgDir{QCoreApplication::applicationDirPath() + QStringLiteral("/../../../../../plugins/") + plgSubDir};
	QVERIFY(bPlgDir.exists());
	QVERIFY(svcDir.mkpath(plgSubDir));
	QVERIFY(svcDir.cd(plgSubDir));
	bPlgDir.setFilter(QDir::NoDotAndDotDot | QDir::Files);
	QDirIterator iter{bPlgDir, QDirIterator::NoIteratorFlags};
	while (iter.hasNext()) {
		iter.next();
		qDebug() << "Found service plugin file:" << iter.fileName();
		QVERIFY(QFile::copy(iter.filePath(), svcDir.absoluteFilePath(iter.fileName())));
	}
	QVERIFY(svcDir.cdUp());

	_manager = OpenSCManagerW(nullptr, nullptr,
							  SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE | STANDARD_RIGHTS_REQUIRED);
	QVERIFY2(_manager, qUtf8Printable(qt_error_string(GetLastError())));
	auto handle = CreateServiceW(_manager,
								 reinterpret_cast<const wchar_t*>(name().utf16()),
								 L"Test Service",
								 SERVICE_CHANGE_CONFIG,
								 SERVICE_WIN32_OWN_PROCESS,
								 SERVICE_DEMAND_START,
								 SERVICE_ERROR_IGNORE,
								 reinterpret_cast<const wchar_t*>(svcArg.utf16()),
								 nullptr,
								 nullptr,
								 nullptr,
								 nullptr,
								 nullptr);
	QVERIFY2(handle, qUtf8Printable(qt_error_string(GetLastError())));

	CloseServiceHandle(handle);
}

void TestWindowsService::cleanup()
{
	// Print eventlog in hopes for some error info:
	if (_printLog)
		QProcess::execute(QStringLiteral("wevtutil qe Application"));

	if(_manager) {
		auto handle = OpenServiceW(_manager,
								   reinterpret_cast<const wchar_t*>(name().utf16()),
								   DELETE);
		QVERIFY2(handle, qUtf8Printable(qt_error_string(GetLastError())));
		QVERIFY2(DeleteService(handle), qUtf8Printable(qt_error_string(GetLastError())));

		CloseServiceHandle(handle);
		CloseServiceHandle(_manager);
		_manager = nullptr;
	}

	qDebug() << "cleanup result:" << _svcDir.remove();
}

void TestWindowsService::testCustomImpl()
{
	_printLog = true;
	testFeature(ServiceControl::SupportFlag::Status);
	QCOMPARE(control->status(), ServiceControl::Status::Running);

	testFeature(ServiceControl::SupportFlag::CustomCommands);
	QVERIFY2(control->callCommand<bool>("command", 142), qUtf8Printable(control->error()));

	QByteArray msg;
	QVariantList args;
	READ_LOOP(msg >> args);
	QCOMPARE(msg, QByteArray{"command"});
	QCOMPARE(args, QVariantList{142});

	testFeature(ServiceControl::SupportFlag::Status);
	QCOMPARE(control->status(), ServiceControl::Status::Running);
	_printLog = false;
}

QTEST_MAIN(TestWindowsService)

#include "tst_windowsservice.moc"
