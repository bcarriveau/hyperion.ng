// Local-Hyperion includes
#include "ProviderRestApi.h"

// Qt includes
#include <QObject>
#include <QEventLoop>
#include <QNetworkReply>
#include <QByteArray>
#include <QJsonObject>

#include <QList>
#include <QHash>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include <QSslSocket>

//std includes
#include <chrono>

// Constants
namespace {

const QChar ONE_SLASH = '/';

enum HttpStatusCode {
	NoContent    = 204,
	BadRequest   = 400,
	UnAuthorized = 401,
	Forbidden    = 403,
	NotFound     = 404,
	TooManyRequests = 429
};

} //End of constants

ProviderRestApi::ProviderRestApi(const QString& scheme, const QString& host, int port, const QString& basePath)
	: _log(Logger::getInstance("LEDDEVICE"))
	, _networkManager(nullptr)
	, _requestTimeout(DEFAULT_REST_TIMEOUT)
	,_isSeflSignedCertificateAccpeted(false)
{
	_networkManager = new QNetworkAccessManager();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
	_networkManager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
	_apiUrl.setScheme(scheme);
	_apiUrl.setHost(host);
	_apiUrl.setPort(port);
	_basePath = basePath;
}

ProviderRestApi::ProviderRestApi(const QString& scheme, const QString& host, int port)
	: ProviderRestApi(scheme, host, port, "") {}

ProviderRestApi::ProviderRestApi(const QString& host, int port, const QString& basePath)
	: ProviderRestApi((port == 443) ? "https" : "http", host, port, basePath) {}

ProviderRestApi::ProviderRestApi(const QString& host, int port)
	: ProviderRestApi(host, port, "") {}

ProviderRestApi::ProviderRestApi()
	: ProviderRestApi("", -1) {}

ProviderRestApi::~ProviderRestApi()
{
	delete _networkManager;
}

void ProviderRestApi::setScheme(const QString& scheme)
{
	_apiUrl.setScheme(scheme);
}

void ProviderRestApi::setUrl(const QUrl& url)
{
	_apiUrl = url;
	_basePath = url.path();
}

void ProviderRestApi::setBasePath(const QStringList& pathElements)
{
	setBasePath(pathElements.join(ONE_SLASH));
}

void ProviderRestApi::setBasePath(const QString& basePath)
{
	_basePath.clear();
	appendPath(_basePath, basePath);
}

void ProviderRestApi::clearBasePath()
{
	_basePath.clear();
}

void ProviderRestApi::setPath(const QStringList& pathElements)
{
	_path.clear();
	appendPath(_path, pathElements.join(ONE_SLASH));
}

void ProviderRestApi::setPath(const QString& path)
{
	_path.clear();
	appendPath(_path, path);
}

void ProviderRestApi::clearPath()
{
	_path.clear();
}

void ProviderRestApi::appendPath(const QString& path)
{
	appendPath(_path, path);
}

void ProviderRestApi::appendPath(const QStringList& pathElements)
{
	appendPath(_path, pathElements.join(ONE_SLASH));
}

void ProviderRestApi::appendPath ( QString& path, const QString &appendPath)
{
	if (!appendPath.isEmpty() && appendPath != ONE_SLASH)
	{
		if (path.isEmpty() || path == ONE_SLASH)
		{
			path.clear();
			if (appendPath[0] != ONE_SLASH)
			{
				path.push_back(ONE_SLASH);
			}
		}
		else if (path[path.size() - 1] == ONE_SLASH && appendPath[0] == ONE_SLASH)
		{
			path.chop(1);
		}
		else if (path[path.size() - 1] != ONE_SLASH && appendPath[0] != ONE_SLASH)
		{
			path.push_back(ONE_SLASH);
		}
		else
		{
			// Only one slash.
		}

		path.append(appendPath);
	}
}

void ProviderRestApi::setFragment(const QString& fragment)
{
	_fragment = fragment;
}

void ProviderRestApi::setQuery(const QUrlQuery& query)
{
	_query = query;
}

QUrl ProviderRestApi::getUrl() const
{
	QUrl url = _apiUrl;

	QString fullPath = _basePath;
	appendPath(fullPath, _path);

	url.setPath(fullPath);
	url.setFragment(_fragment);
	url.setQuery(_query);
	return url;
}

httpResponse ProviderRestApi::get()
{
	return get(getUrl());
}

httpResponse ProviderRestApi::get(const QUrl& url)
{
	return executeOperation(QNetworkAccessManager::GetOperation, url);
}

httpResponse ProviderRestApi::put(const QJsonObject &body)
{
	return put( getUrl(), QJsonDocument(body).toJson(QJsonDocument::Compact));
}

httpResponse ProviderRestApi::put(const QString &body)
{
	return put( getUrl(), body.toUtf8() );
}

httpResponse ProviderRestApi::put(const QUrl &url, const QByteArray &body)
{
	return executeOperation(QNetworkAccessManager::PutOperation, url, body);
}

httpResponse ProviderRestApi::post(const QJsonObject& body)
{
	return post( getUrl(), QJsonDocument(body).toJson(QJsonDocument::Compact));
}

httpResponse ProviderRestApi::post(const QString& body)
{
	return post( getUrl(), body.toUtf8() );
}

httpResponse ProviderRestApi::post(const QUrl& url, const QByteArray& body)
{
	return executeOperation(QNetworkAccessManager::PostOperation, url, body);
}

httpResponse ProviderRestApi::deleteResource(const QUrl& url)
{
	return executeOperation(QNetworkAccessManager::DeleteOperation, url);
}

httpResponse ProviderRestApi::executeOperation(QNetworkAccessManager::Operation operation, const QUrl& url, const QByteArray& body)
{
	// Perform request
	QNetworkRequest request(_networkRequestHeaders);
	request.setUrl(url);
	request.setOriginatingObject(this);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	_networkManager->setTransferTimeout(_requestTimeout.count());
#endif

	QDateTime const start = QDateTime::currentDateTime();
	QString opCode;
	QNetworkReply* reply;

	switch (operation) {
	case QNetworkAccessManager::GetOperation:
		opCode = "GET";
		reply = _networkManager->get(request);
		break;
	case QNetworkAccessManager::PutOperation:
		opCode = "PUT";
		reply = _networkManager->put(request, body);
		break;
	case QNetworkAccessManager::PostOperation:
		opCode = "POST";
		reply = _networkManager->post(request, body);
		break;
	case QNetworkAccessManager::DeleteOperation:
		opCode = "DELETE";
		reply = _networkManager->deleteResource(request);
		break;
	default:
		Error(_log, "Unsupported operation");
		return httpResponse();
	}

	// Connect requestFinished signal to quit slot of the loop.
	QEventLoop loop;
	QEventLoop::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
	ReplyTimeout* timeout = ReplyTimeout::set(reply, _requestTimeout.count());
#endif

	// Go into the loop until the request is finished.
	loop.exec();
	QDateTime const end = QDateTime::currentDateTime();

	httpResponse response = (reply->operation() == operation) ? getResponse(reply) : httpResponse();

	Debug(_log, "%s took %lldms, HTTP %d: [%s] [%s]", QSTRING_CSTR(opCode), start.msecsTo(end), response.getHttpStatusCode(), QSTRING_CSTR(url.toString()), body.constData());

	// Free space.
	reply->deleteLater();

	return response;
}

httpResponse ProviderRestApi::getResponse(QNetworkReply* const& reply)
{
	httpResponse response;

	HttpStatusCode const httpStatusCode = static_cast<HttpStatusCode>(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
	response.setHttpStatusCode(httpStatusCode);
	response.setNetworkReplyError(reply->error());
	response.setHeaders(reply->rawHeaderPairs());

	if (reply->error() == QNetworkReply::NoError)
	{
		QByteArray const replyData = reply->readAll();
		if (!replyData.isEmpty())
		{
			QJsonParseError error;
			QJsonDocument const jsonDoc = QJsonDocument::fromJson(replyData, &error);

			if (error.error != QJsonParseError::NoError)
			{
				//Received not valid JSON response
				response.setError(true);
				response.setErrorReason(error.errorString());
			}
			else
			{
				response.setBody(jsonDoc);
			}
		}
		else
		{	// Create valid body which is empty
			response.setBody(QJsonDocument());
		}
	}
	else
	{
		QString errorReason;
		if (reply->error() == QNetworkReply::OperationCanceledError)
		{
			errorReason = "Network request timeout error";
		}
		else
		{
			if (httpStatusCode > 0) {
				QString const httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
				QString advise;
				switch ( httpStatusCode ) {
				case HttpStatusCode::BadRequest:
					advise = "Check Request Body";
					break;
				case HttpStatusCode::UnAuthorized:
					advise = "Check Authorization Token (API Key)";
					break;
				case HttpStatusCode::Forbidden:
					advise = "No permission to access the given resource";
					break;
				case HttpStatusCode::NotFound:
					advise = "Check Resource given";
					break;
				case HttpStatusCode::TooManyRequests:
				{
					QString const retryAfterTime = response.getHeader("Retry-After");
					if (!retryAfterTime.isEmpty())
					{
						advise = "Retry-After: " + response.getHeader("Retry-After");
					}
				}
				break;
				default:
					advise = httpReason;
					break;
				}
				errorReason = QString ("[%3 %4] - %5").arg(httpStatusCode).arg(httpReason, advise);
			}
			else
			{
				errorReason = reply->errorString();
			}
		}

		response.setError(true);
		response.setErrorReason(errorReason);
	}
	return response;
}

void ProviderRestApi::setHeader(QNetworkRequest::KnownHeaders header, const QVariant& value)
{
	QVariant const headerValue = _networkRequestHeaders.header(header);
	if (headerValue.isNull())
	{
		_networkRequestHeaders.setHeader(header, value);
	}
	else
	{
		if (!headerValue.toString().contains(value.toString()))
		{
			_networkRequestHeaders.setHeader(header, headerValue.toString() + "," + value.toString());
		}
	}
}

void ProviderRestApi::setHeader(const QByteArray &headerName, const QByteArray &headerValue)
{
	_networkRequestHeaders.setRawHeader(headerName, headerValue);
}

void httpResponse::setHeaders(const QList<QNetworkReply::RawHeaderPair>& pairs)
{
	_responseHeaders.clear();
	for (const auto &item: pairs)
	{
		_responseHeaders[item.first] = item.second;
	}
}

QByteArray httpResponse::getHeader(const QByteArray& header) const
{
	return _responseHeaders.value(header);
}

bool ProviderRestApi::setCaCertificate(const QString& caFileName)
{
	bool rc {false};
	/// Add our own CA to the default SSL configuration
	QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();

	QFile caFile (caFileName);
	if (!caFile.open(QIODevice::ReadOnly))
	{
		Error(_log,"Unable to open CA-Certificate file: %s", QSTRING_CSTR(caFileName));
		return false;
	}

	QSslCertificate const cert (&caFile);
	caFile.close();

	QList<QSslCertificate> allowedCAs;
	allowedCAs << cert;
	configuration.setCaCertificates(allowedCAs);

	QSslConfiguration::setDefaultConfiguration(configuration);

#ifndef QT_NO_SSL
	if (QSslSocket::supportsSsl())
	{
		QObject::connect( _networkManager, &QNetworkAccessManager::sslErrors, this, &ProviderRestApi::onSslErrors );
		_networkManager->connectToHostEncrypted(_apiUrl.host(), static_cast<quint16>(_apiUrl.port()), configuration);
		rc = true;
	}
#endif

	return rc;
}

void ProviderRestApi::acceptSelfSignedCertificates(bool isAccepted)
{
	_isSeflSignedCertificateAccpeted = isAccepted;
}

void ProviderRestApi::setAlternateServerIdentity(const QString& serverIdentity)
{
	_serverIdentity = serverIdentity;
}

QString ProviderRestApi::getAlternateServerIdentity() const
{
	return _serverIdentity;
}

bool ProviderRestApi::checkServerIdentity(const QSslConfiguration& sslConfig) const
{
	bool isServerIdentified {false};

	// Perform common name validation
	QSslCertificate const serverCertificate = sslConfig.peerCertificate();
	QStringList const commonName = serverCertificate.subjectInfo(QSslCertificate::CommonName);
	if ( commonName.contains(getAlternateServerIdentity(), Qt::CaseInsensitive) )
	{
		isServerIdentified = true;
	}

	return isServerIdentified;
}

bool ProviderRestApi::matchesPinnedCertificate(const QSslCertificate& certificate)
{
	bool isMatching {false};

	QList const certificateInfos = certificate.subjectInfo(QSslCertificate::CommonName);

	if (certificateInfos.isEmpty())
	{
		return false;
	}
	QString const& identifier = certificateInfos.constFirst();
	QString const appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QString const certDir = appDataDir + "/certificates";

	Debug (_log,"Directory used for pinned certificates: %s", QSTRING_CSTR(certDir));

	bool const isMkPath = QDir().mkpath(certDir);
	if (!isMkPath)
	{
		Error (_log,"Failed to create directory \"%s\" to store pinned certificates. 'Trust on first use' is rejected.", QSTRING_CSTR(certDir));
		return false;
	}

	QString const fileName(identifier + ".pem");
	QString const filePath(certDir + "/" + fileName);
	QFile file(filePath);

	if (file.open(QIODevice::ReadOnly))
	{
		QList const certificates = QSslCertificate::fromDevice(&file, QSsl::Pem);
		if (!certificates.isEmpty())
		{
			Debug (_log,"First used certificate \"%s\" loaded successfully", QSTRING_CSTR(fileName));
			QSslCertificate const& pinnedeCertificate = certificates.constFirst();
			if (pinnedeCertificate == certificate)
			{
				isMatching = true;
			}
		}
		else
		{
			Debug (_log,"Error reading first used certificate file: %s", QSTRING_CSTR(filePath));
		}
		file.close();
	}
	else
	{
		if (file.open(QIODevice::WriteOnly))
		{
			QByteArray const pemData = certificate.toPem();
			qint64 const bytesWritten = file.write(pemData);
			if (bytesWritten == pemData.size())
			{
				Debug (_log,"First used certificate saved to file: %s", QSTRING_CSTR(filePath));
				isMatching = true;
			}
			else
			{
				Debug (_log,"Error writing first used certificate file: %s", QSTRING_CSTR(filePath));
			}
			file.close();
		}
	}
	return isMatching;
}

void ProviderRestApi::onSslErrors(QNetworkReply* reply, const QList<QSslError>& errors)
{
	int ignoredErrorCount {0};
	for (const QSslError &error : errors)
	{
		bool ignoreSslError{false};

		switch (error.error()) {
		case QSslError::HostNameMismatch :
			if (checkServerIdentity(reply->sslConfiguration()) )
			{
				ignoreSslError = true;
			}
			break;
		case QSslError::SelfSignedCertificate :
		if (_isSeflSignedCertificateAccpeted)
		{
			// Get the peer certificate associated with the error
			QSslCertificate const certificate = error.certificate();
			if (matchesPinnedCertificate(certificate))
			{
				Debug (_log,"'Trust on first use' - Certificate received matches pinned certificate");
				ignoreSslError = true;
			}
			else
			{
				Error (_log,"'Trust on first use' - Certificate received does not match pinned certificate");
			}
		}
		break;
		default:
			break;
		}

		if (ignoreSslError)
		{
			++ignoredErrorCount;
		}
		else
		{
			Debug (_log,"SSL Error occured: [%d] %s ",error.error(), QSTRING_CSTR(error.errorString()));
		}
	}

	if (ignoredErrorCount == errors.size())
	{
		reply->ignoreSslErrors();
	}
}
