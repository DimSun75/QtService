#include "launchdserviceplugin.h"
#include "launchdservicebackend.h"

LaunchdServicePlugin::LaunchdServicePlugin(QObject *parent) :
	QObject{parent}
{}

QtService::ServiceBackend *LaunchdServicePlugin::createServiceBackend(const QString &provider, QtService::Service *service)
{
	if(provider == QStringLiteral("launchd"))
		return new LaunchdServiceBackend{service};
	else
		return nullptr;
}

QtService::ServiceControl *LaunchdServicePlugin::createServiceControl(const QString &backend, QString &&serviceId, QObject *parent)
{
	return nullptr;
}