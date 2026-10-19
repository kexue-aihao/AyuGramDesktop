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
#include "base/weak_ptr.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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
			weak->showToast(tr::ayu_UpdateChecking());

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
			QObject::connect(reply, &QNetworkReply::finished, manager.get(),
				[=] {
					const auto status = reply->attribute(
						QNetworkRequest::HttpStatusCodeAttribute).toInt();
					if (reply->error() != QNetworkReply::NoError
						|| status < 200
						|| status >= 300) {
						if (weak) {
							weak->showToast(tr::ayu_UpdateCheckFailed());
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
							weak->showToast(tr::ayu_UpdateCheckFailed());
						}
						reply->deleteLater();
						return;
					}

					const auto currentNumber = QString::fromLatin1(
						Ayu::kReleaseTag).section('-', -1).toLongLong();
					const auto remoteNumber = tag.section('-', -1).toLongLong();
					const auto updateAvailable = (currentNumber > 0
						&& remoteNumber > 0)
						? (remoteNumber > currentNumber)
						: (tag != QString::fromLatin1(Ayu::kReleaseTag));
					if (!updateAvailable) {
						if (weak) {
							weak->showToast(tr::ayu_UpdateLatest());
						}
					} else {
						if (weak) {
							weak->showToast(u"发现新版本："_q + tag);
						}
						QDesktopServices::openUrl(QUrl(url));
					}
					reply->deleteLater();
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
