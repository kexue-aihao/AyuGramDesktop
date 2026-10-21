// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_version.h"
#include "ayu/ui/ayu_logo.h"
#include "ayu/ui/settings/settings_appearance.h"
#include "ayu/ui/settings/settings_ayu.h"
#include "ayu/ui/settings/settings_chats.h"
#include "ayu/ui/settings/settings_filters.h"
#include "ayu/ui/settings/settings_general.h"
#include "ayu/ui/settings/settings_other.h"
#include "base/platform/base_platform_info.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QUrl>

#include <functional>
#include <memory>

namespace Settings {

using namespace Builder;

namespace {

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = AyuAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto size = st::settingsCloudPasswordIconSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

[[nodiscard]] QString UpdateAssetSuffix() {
	if (Platform::IsWindowsARM64()) {
		return u"-Windows-arm64-setup.exe"_q;
	} else if (Platform::IsWindows64Bit()) {
		return u"-Windows-x64-setup.exe"_q;
	}
	return u"-Windows-x86-setup.exe"_q;
}

[[nodiscard]] QString FileSha256(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		hash.addData(file.read(1 << 20));
	}
	return QString::fromLatin1(hash.result().toHex());
}

struct UpdateDownloadState {
	QString installerUrl;
	QString installerName;
	QString sumsUrl;
	QString installerPath;
	QByteArray sums;
	bool sumsFinished = false;
	bool installerFinished = false;
	bool failed = false;
};

void DownloadUpdate(
		base::weak_ptr<Window::SessionController> weak,
		std::shared_ptr<QNetworkAccessManager> manager,
		std::shared_ptr<UpdateDownloadState> state) {
	const auto directory = QStandardPaths::writableLocation(
		QStandardPaths::TempLocation) + u"/AyuGramUpdate"_q;
	state->installerPath = directory + u"/"_q + state->installerName;
	QFile::remove(state->installerPath);

	const auto file = std::make_shared<QFile>(state->installerPath);
	const auto fail = [=] {
		if (state->failed) {
			return;
		}
		state->failed = true;
		if (file->isOpen()) {
			file->close();
		}
		QFile::remove(state->installerPath);
		if (weak) {
			weak->showToast(tr::ayu_UpdateDownloadFailed(tr::now));
		}
	};

	if (!QDir().mkpath(directory) || !file->open(QIODevice::WriteOnly)) {
		fail();
		return;
	}

	const auto finish = std::make_shared<std::function<void()>>();
	*finish = [=] {
		if (state->failed
			|| !state->sumsFinished
			|| !state->installerFinished) {
			return;
		}
		const auto hash = FileSha256(state->installerPath);
		auto expected = QString();
		const auto lines = QString::fromUtf8(state->sums).split(u'\n');
		for (const auto &line : lines) {
			const auto parts = line.simplified().split(u' ');
			if (parts.size() >= 2 && parts.last() == state->installerName) {
				expected = parts.first();
				break;
			}
		}
		if (hash.isEmpty()
			|| expected.isEmpty()
			|| hash.compare(expected, Qt::CaseInsensitive) != 0) {
			state->failed = true;
			QFile::remove(state->installerPath);
			if (weak) {
				weak->showToast(tr::ayu_UpdateVerifyFailed(tr::now));
			}
			return;
		}
		if (!weak) {
			return;
		}
		const auto installerPath = state->installerPath;
		weak->show(Ui::MakeConfirmBox({
			.text = tr::ayu_UpdateReadyText(),
			.confirmed = [=](Fn<void()> &&close) {
				close();
				QProcess::startDetached(installerPath, QStringList());
				Core::Quit();
			},
			.confirmText = tr::ayu_UpdateInstallNow(),
			.cancelText = tr::ayu_UpdateInstallLater(),
			.title = tr::ayu_UpdateReadyTitle(),
		}));
	};

	const auto makeRequest = [](const QString &url) {
		auto request = QNetworkRequest(QUrl(url));
		request.setHeader(
			QNetworkRequest::UserAgentHeader,
			u"AyuGramDesktop/"_q + QString::fromLatin1(Ayu::kReleaseTag));
		request.setAttribute(
			QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::NoLessSafeRedirectPolicy);
		request.setTransferTimeout(60 * 1000);
		return request;
	};

	const auto installerReply = manager->get(makeRequest(state->installerUrl));
	QObject::connect(
		installerReply,
		&QNetworkReply::readyRead,
		manager.get(),
		[=] {
			if (!state->failed) {
				file->write(installerReply->readAll());
			}
		});
	QObject::connect(
		installerReply,
		&QNetworkReply::finished,
		manager.get(),
		[=] {
			if (state->failed) {
				installerReply->deleteLater();
				return;
			}
			const auto status = installerReply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			file->write(installerReply->readAll());
			file->close();
			if (installerReply->error() != QNetworkReply::NoError
				|| status < 200
				|| status >= 300) {
				fail();
			} else {
				state->installerFinished = true;
				(*finish)();
			}
			installerReply->deleteLater();
		});

	const auto sumsReply = manager->get(makeRequest(state->sumsUrl));
	QObject::connect(
		sumsReply,
		&QNetworkReply::finished,
		manager.get(),
		[=] {
			if (state->failed) {
				sumsReply->deleteLater();
				return;
			}
			const auto status = sumsReply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (sumsReply->error() != QNetworkReply::NoError
				|| status < 200
				|| status >= 300) {
				fail();
			} else {
				state->sums = sumsReply->readAll();
				state->sumsFinished = true;
				(*finish)();
			}
			sumsReply->deleteLater();
		});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(QString::fromLatin1(Ayu::kVersion)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	const auto controller = builder.controller();
	const auto weak = base::make_weak(controller);
	const auto manager = std::make_shared<QNetworkAccessManager>();
	builder.addButton({
		.id = u"ayu/checkUpdates"_q,
		.title = tr::ayu_CheckForUpdates(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			if (!weak) {
				return;
			}
			weak->showToast(tr::ayu_UpdateChecking(tr::now));

			auto request = QNetworkRequest(QUrl(
				u"https://api.github.com/repos/"_q
				+ QString::fromLatin1(Ayu::kReleaseRepository)
				+ u"/releases/latest"_q));
			request.setHeader(
				QNetworkRequest::UserAgentHeader,
				u"AyuGramDesktop/"_q + QString::fromLatin1(Ayu::kReleaseTag));
			request.setAttribute(
				QNetworkRequest::RedirectPolicyAttribute,
				QNetworkRequest::NoLessSafeRedirectPolicy);
			request.setTransferTimeout(15 * 1000);

			const auto reply = manager->get(request);
			QObject::connect(reply, &QNetworkReply::finished, manager.get(), [=] {
				const auto status = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute).toInt();
				if (reply->error() != QNetworkReply::NoError
					|| status < 200
					|| status >= 300) {
					if (weak) {
						weak->showToast(tr::ayu_UpdateCheckFailed(tr::now));
					}
					reply->deleteLater();
					return;
				}

				auto error = QJsonParseError{};
				const auto document = QJsonDocument::fromJson(
					reply->readAll(), &error);
				const auto object = document.object();
				const auto tag = object.value(u"tag_name"_q).toString();
				const auto url = object.value(u"html_url"_q).toString();
				if (error.error != QJsonParseError::NoError
					|| !document.isObject()
					|| tag.isEmpty()
					|| url.isEmpty()) {
					if (weak) {
						weak->showToast(tr::ayu_UpdateCheckFailed(tr::now));
					}
					reply->deleteLater();
					return;
				}

				const auto releaseNumber = [](const QString &value) {
					auto digits = QString();
					for (const auto ch : value) {
						if (ch.isDigit()) {
							digits.append(ch);
						}
					}
					return digits.toLongLong();
				};
				const auto currentNumber = releaseNumber(
					QString::fromLatin1(Ayu::kReleaseTag));
				const auto remoteNumber = releaseNumber(tag);
				const auto updateAvailable = (currentNumber > 0
					&& remoteNumber > 0)
					? (remoteNumber > currentNumber)
					: (tag != QString::fromLatin1(Ayu::kReleaseTag));
				if (!updateAvailable) {
					if (weak) {
						weak->showToast(tr::ayu_UpdateLatest(tr::now));
					}
					reply->deleteLater();
					return;
				}

				auto installerName = QString();
				auto installerUrl = QString();
				auto sumsUrl = QString();
				const auto suffix = UpdateAssetSuffix();
				for (const auto &value : object.value(u"assets"_q).toArray()) {
					const auto asset = value.toObject();
					const auto name = asset.value(u"name"_q).toString();
					const auto download = asset.value(
						u"browser_download_url"_q).toString();
					if (name.endsWith(suffix)) {
						installerName = name;
						installerUrl = download;
					} else if (name == u"SHA256SUMS.txt"_q) {
						sumsUrl = download;
					}
				}
				reply->deleteLater();

				if (!weak) {
					return;
				}
				if (installerName.isEmpty()
					|| installerUrl.isEmpty()
					|| sumsUrl.isEmpty()) {
					weak->showToast(u"发现新版本："_q + tag);
					QDesktopServices::openUrl(QUrl(url));
					return;
				}

				const auto state = std::make_shared<UpdateDownloadState>();
				state->installerName = installerName;
				state->installerUrl = installerUrl;
				state->sumsUrl = sumsUrl;
				weak->show(Ui::MakeConfirmBox({
					.text = tag + u" 已发布，是否下载并安装？"_q,
					.confirmed = [=](Fn<void()> &&close) {
						close();
						if (weak) {
							weak->showToast(tr::ayu_UpdateDownloading(tr::now));
						}
						DownloadUpdate(weak, manager, state);
					},
					.cancelled = [=](Fn<void()> &&close) {
						close();
						QDesktopServices::openUrl(QUrl(url));
					},
					.confirmText = tr::ayu_UpdateDownloadAndInstall(),
					.cancelText = tr::ayu_UpdateOpenReleasePage(),
					.title = tr::ayu_UpdateAvailableTitle(),
					.strictCancel = true,
				}));
			});
		},
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				tr::ayu_SettingsDescription(),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_CategoriesHeader());

	builder.addSectionButton({
		.title = rpl::single(QString("AyuGram")),
		.targetSection = AyuGhost::Id(),
		.icon = { &st::menuIconGroupReactions },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryFilters(),
		.targetSection = AyuFilters::Id(),
		.icon = { &st::menuIconTagFilter },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryGeneral(),
		.targetSection = AyuGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryAppearance(),
		.targetSection = AyuAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryChats(),
		.targetSection = AyuChats::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryOther(),
		.targetSection = AyuOther::Id(),
		.icon = { &st::menuIconFave },
	});
}

void BuildLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.id = u"ayu/channel"_q,
		.title = tr::ayu_LinksChannel(),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@ayugram")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugram"),
			});
		},
	});
	builder.addButton({
		.id = u"ayu/chat"_q,
		.title = tr::ayu_LinksChats(),
		.icon = { &st::menuIconChats },
		.label = rpl::single(QString("@ayugramchat")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugramchat"),
			});
		},
	});
	builder.addButton({
		.id = u"ayu/crowdin"_q,
		.title = tr::ayu_LinksTranslate(),
		.icon = { &st::menuIconTranslate },
		.label = rpl::single(QString("Crowdin")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://translate.ayugram.one"));
		},
	});
	builder.addButton({
		.id = u"ayu/website"_q,
		.title = tr::ayu_LinksDocumentation(),
		.icon = { &st::menuIconIpAddress },
		.label = rpl::single(QString("docs.ayugram.one")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://docs.ayugram.one"));
		},
	});

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuMain::Id(),
	.parentId = MainId(),
	.title = &tr::ayu_AyuPreferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildCategories(builder);
	BuildLinks(builder);
});

} // namespace

rpl::producer<QString> AyuMain::title() {
	return rpl::single(QString(""));
}

AyuMain::AyuMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuMainId() {
	return AyuMain::Id();
}

} // namespace Settings
