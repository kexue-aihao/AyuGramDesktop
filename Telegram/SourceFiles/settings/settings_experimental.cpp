/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"

#include "settings/settings_common.h"
#include "data/components/passkeys.h"
#include "ui/layers/generic_box.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/search_field_controller.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/kinetic_scroller.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "ui/gl/gl_detection.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "base/options.h"
#include "boxes/moderate_messages_box.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/sandbox.h"
#include "chat_helpers/tabbed_panel.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "history/history_item_components.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "media/player/media_player_instance.h"
#include "mtproto/session_private.h"
#include "webview/webview_embed.h"
#include "window/main_window.h"
#include "window/window_filters_favorite.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "info/info_flexible_scroll.h"
#include "chat_helpers/stickers_list_widget.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QGuiApplication>

#include <array>

// AyuGram includes
#include "ayu/ui/settings/settings_main.h"
#include "settings/settings_builder.h"


namespace Settings {
namespace {

const auto kOptionsClipboardPrefix = u"tdesktop-flags:"_q;

struct DecodeOptionsResult {
	bool ok = false;
	QString json;
};

struct ResolvedReferrer {
	QString controlId;
	Type section = AyuMain::Id();
};

[[nodiscard]] QString EncodeOptionsToText(const QString &json) {
	const auto flags = QByteArray::Base64UrlEncoding
		| QByteArray::OmitTrailingEquals;
	return kOptionsClipboardPrefix
		+ qs(qCompress(json.toLatin1(), 9).toBase64(flags));
}

[[nodiscard]] DecodeOptionsResult DecodeOptionsFromText(const QString &text) {
	auto result = DecodeOptionsResult();
	if (!text.startsWith(kOptionsClipboardPrefix)) {
		return result;
	}
	auto encoded = QStringView(text).mid(
		kOptionsClipboardPrefix.size()).toLatin1();
	const auto compressed = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!compressed || (*compressed).isEmpty()) {
		return result;
	}
	const auto decoded = qUncompress(*compressed);
	if (decoded.isEmpty()) {
		return result;
	}

	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(decoded, &error);
	if ((error.error != QJsonParseError::NoError) || !parsed.isObject()) {
		return result;
	}
	result.ok = true;
	result.json = QString::fromUtf8(decoded);
	return result;
}

[[nodiscard]] ResolvedReferrer ResolveReferrer(
		const QString &controlId,
		not_null<Main::Session*> session) {
	const auto &registry = Builder::SearchRegistry::Instance();
	const auto entries = registry.collectAll(session);
	for (const auto &entry : entries) {
		if (!entry.section) {
			continue;
		}
		if (entry.id == controlId) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
		if (entry.altIds.contains(controlId)) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
	}
	return {
		.controlId = controlId,
	};
}

[[nodiscard]] QString OptionReferrer(const base::options::option<bool> &option) {
	const auto &id = option.id();
	if (id == u"tabbed-panel-show-on-click"_q) {
		return u"ayu/showEmojiPopup"_q;
	} else if (id == u"show-peer-id-below-about"_q) {
		return u"ayu/showPeerId"_q;
	} else if (id == u"use-small-msg-bubble-radius"_q) {
		return u"ayu/messageBubbleRadius"_q;
	} else if (id == u"unlimited-recent-stickers"_q) {
		return u"ayu/unlimitedRecentStickers"_q;
	} else if (id == u"hide-ai-button"_q) {
		return u"ayu/showAiEditorButtonInMessageField"_q;
	} else if (id == u"unlimited-message-width"_q) {
		return u"ayu/wideMultiplier"_q;
	}
	return QString();
}

struct OptionTranslation {
	const char *id;
	const tr::phrase<> *name;
	const tr::phrase<> *description;
};

[[nodiscard]] const auto &OptionTranslations() {
	static const auto result = std::array{
		OptionTranslation{
			"dialogs-mute-icon",
			&tr::lng_settings_experimental_option_dialogs_mute_icon,
			&tr::lng_settings_experimental_option_dialogs_mute_icon_about,
		},
		OptionTranslation{
			"forum-hide-chats-list",
			&tr::lng_settings_experimental_option_forum_hide_chats_list,
			&tr::lng_settings_experimental_option_forum_hide_chats_list_about,
		},
		OptionTranslation{
			"dialogs-unread-on-top",
			&tr::lng_settings_experimental_option_dialogs_unread_on_top,
			&tr::lng_settings_experimental_option_dialogs_unread_on_top_about,
		},
		OptionTranslation{
			"use-new-chat-view",
			&tr::lng_settings_experimental_option_use_new_chat_view,
			&tr::lng_settings_experimental_option_use_new_chat_view_about,
		},
		OptionTranslation{
			"auto-scroll-inactive-chat",
			&tr::lng_settings_experimental_option_auto_scroll_inactive_chat,
			&tr::lng_settings_experimental_option_auto_scroll_inactive_chat_about,
		},
		OptionTranslation{
			"moderate-common-groups",
			&tr::lng_settings_experimental_option_moderate_common_groups,
			nullptr,
		},
		OptionTranslation{
			"classic-profile-scroll",
			&tr::lng_settings_experimental_option_classic_profile_scroll,
			&tr::lng_settings_experimental_option_classic_profile_scroll_about,
		},
		OptionTranslation{
			"use-small-msg-bubble-radius",
			&tr::lng_settings_experimental_option_use_small_msg_bubble_radius,
			&tr::lng_settings_experimental_option_use_small_msg_bubble_radius_about,
		},
		OptionTranslation{
			"unlimited-message-width",
			&tr::lng_settings_experimental_option_unlimited_message_width,
			&tr::lng_settings_experimental_option_unlimited_message_width_about,
		},
		OptionTranslation{
			"mac-cmd-reply-immediately",
			&tr::lng_settings_experimental_option_mac_cmd_reply_immediately,
			&tr::lng_settings_experimental_option_mac_cmd_reply_immediately_about,
		},
		OptionTranslation{
			"hide-ai-button",
			&tr::lng_settings_experimental_option_hide_ai_button,
			&tr::lng_settings_experimental_option_hide_ai_button_about,
		},
		OptionTranslation{
			"force-compose-search-one-column",
			&tr::lng_settings_experimental_option_force_compose_search_one_column,
			&tr::lng_settings_experimental_option_force_compose_search_one_column_about,
		},
		OptionTranslation{
			"view-profile-in-chats-list-context-menu",
			&tr::lng_settings_experimental_option_view_profile_in_chats_list_context_menu,
			&tr::lng_settings_experimental_option_view_profile_in_chats_list_context_menu_about,
		},
		OptionTranslation{
			"show-peer-id-below-about",
			&tr::lng_settings_experimental_option_show_peer_id_below_about,
			&tr::lng_settings_experimental_option_show_peer_id_below_about_about,
		},
		OptionTranslation{
			"show-channel-joined-below-about",
			&tr::lng_settings_experimental_option_show_channel_joined_below_about,
			&tr::lng_settings_experimental_option_show_channel_joined_below_about_about,
		},
		OptionTranslation{
			"profile-media-tabs",
			&tr::lng_settings_experimental_option_profile_media_tabs,
			&tr::lng_settings_experimental_option_profile_media_tabs_about,
		},
		OptionTranslation{
			"profile-media-tabs-expanded",
			&tr::lng_settings_experimental_option_profile_media_tabs_expanded,
			&tr::lng_settings_experimental_option_profile_media_tabs_expanded_about,
		},
		OptionTranslation{
			"tabbed-panel-show-on-click",
			&tr::lng_settings_experimental_option_tabbed_panel_show_on_click,
			&tr::lng_settings_experimental_option_tabbed_panel_show_on_click_about,
		},
		OptionTranslation{
			"unlimited-recent-stickers",
			&tr::lng_settings_experimental_option_unlimited_recent_stickers,
			&tr::lng_settings_experimental_option_unlimited_recent_stickers_about,
		},
		OptionTranslation{
			"disable-autoplay-next",
			&tr::lng_settings_experimental_option_disable_autoplay_next,
			&tr::lng_settings_experimental_option_disable_autoplay_next_about,
		},
		OptionTranslation{
			"external-media-viewer",
			&tr::lng_settings_experimental_option_external_media_viewer,
			&tr::lng_settings_experimental_option_external_media_viewer_about,
		},
		OptionTranslation{
			"ffmpeg-multithread",
			&tr::lng_settings_experimental_option_ffmpeg_multithread,
			&tr::lng_settings_experimental_option_ffmpeg_multithread_about,
		},
		OptionTranslation{
			"hide-reply-button",
			&tr::lng_settings_experimental_option_hide_reply_button,
			&tr::lng_settings_experimental_option_hide_reply_button_about,
		},
		OptionTranslation{
			"custom-notification",
			&tr::lng_settings_experimental_option_custom_notification,
			&tr::lng_settings_experimental_option_custom_notification_about,
		},
		OptionTranslation{
			"gnotification",
			&tr::lng_settings_experimental_option_gnotification,
			&tr::lng_settings_experimental_option_gnotification_about,
		},
		OptionTranslation{
			"mac-modern-notifications",
			&tr::lng_settings_experimental_option_mac_modern_notifications,
			&tr::lng_settings_experimental_option_mac_modern_notifications_about,
		},
		OptionTranslation{
			"fractional-scaling-enabled",
			&tr::lng_settings_experimental_option_fractional_scaling_enabled,
			&tr::lng_settings_experimental_option_fractional_scaling_enabled_about,
		},
		OptionTranslation{
			"high-dpi-downscale",
			&tr::lng_settings_experimental_option_high_dpi_downscale,
			&tr::lng_settings_experimental_option_high_dpi_downscale_about,
		},
		OptionTranslation{
			"use-qt-rhi",
			&tr::lng_settings_experimental_option_use_qt_rhi,
			&tr::lng_settings_experimental_option_use_qt_rhi_about,
		},
		OptionTranslation{
			"enable-vulkan-rhi",
			&tr::lng_settings_experimental_option_enable_vulkan_rhi,
			&tr::lng_settings_experimental_option_enable_vulkan_rhi_about,
		},
		OptionTranslation{
			"freetype",
			&tr::lng_settings_experimental_option_freetype,
			&tr::lng_settings_experimental_option_freetype_about,
		},
		OptionTranslation{
			"kinetic-scroller",
			&tr::lng_settings_experimental_option_kinetic_scroller,
			nullptr,
		},
		OptionTranslation{
			"touchbar-disabled",
			&tr::lng_settings_experimental_option_touchbar_disabled,
			nullptr,
		},
		OptionTranslation{
			"new-windows-size-as-first",
			&tr::lng_settings_experimental_option_new_windows_size_as_first,
			&tr::lng_settings_experimental_option_new_windows_size_as_first_about,
		},
		OptionTranslation{
			"prefer-ipv6",
			&tr::lng_settings_experimental_option_prefer_ipv6,
			&tr::lng_settings_experimental_option_prefer_ipv6_about,
		},
		OptionTranslation{
			"skip-url-scheme-register",
			&tr::lng_settings_experimental_option_skip_url_scheme_register,
			&tr::lng_settings_experimental_option_skip_url_scheme_register_about,
		},
		OptionTranslation{
			"deadlock-detector",
			&tr::lng_settings_experimental_option_deadlock_detector,
			&tr::lng_settings_experimental_option_deadlock_detector_about,
		},
		OptionTranslation{
			"webview-debug-enabled",
			&tr::lng_settings_experimental_option_webview_debug_enabled,
			&tr::lng_settings_experimental_option_webview_debug_enabled_about,
		},
		OptionTranslation{
			"webview-legacy-edge",
			&tr::lng_settings_experimental_option_webview_legacy_edge,
			&tr::lng_settings_experimental_option_webview_legacy_edge_about,
		},
		OptionTranslation{
			"fast-buttons-mode",
			&tr::lng_settings_experimental_option_fast_buttons_mode,
			&tr::lng_settings_experimental_option_fast_buttons_mode_about,
		},
		OptionTranslation{
			"folder-favorite-link",
			&tr::lng_settings_experimental_option_folder_favorite_link,
			&tr::lng_settings_experimental_option_folder_favorite_link_about,
		},
	};
	return result;
}

struct OptionText {
	QString name;
	QString description;
};

[[nodiscard]] OptionText LocalizedOptionText(
		const QString &id,
		const QString &name,
		const QString &description) {
	for (const auto &translation : OptionTranslations()) {
		if (id == QString::fromLatin1(translation.id)) {
			return {
				.name = (*translation.name)(tr::now),
				.description = translation.description
					? (*translation.description)(tr::now)
					: QString(),
			};
		}
	}
	return { .name = name, .description = description };
}

void SetupCopyDeepLink(
		not_null<Window::Controller*> window,
		not_null<Button*> button,
		const QString &id) {
	const auto link = u"tg://settings/experimental/"_q + id;
	const auto menu
		= button->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(
			tr::lng_settings_experimental_copy_link(tr::now),
			[=] {
				TextUtilities::SetClipboardText({ link });
				window->showToast({
					.text = {
						tr::lng_settings_experimental_deep_link_copied(tr::now)
					},
					.iconLottie = u"toast/voip_invite"_q,
					.iconLottieSize = st::toastLottieIconSize,
				});
			},
			&st::menuIconCopy);
		(*menu)->popup(QCursor::pos());
		e->accept();
	}, button->lifetime());
}

[[nodiscard]] not_null<Button*> AddOptionRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &name,
		const QString &description,
		const style::SettingsButton &st) {
	if (description.isEmpty()) {
		return container->add(object_ptr<Button>(
			container,
			rpl::single(name),
			st));
	}
	const auto &titlePadding = st::settingsExperimentalTitlePadding;
	const auto &aboutPadding = st::settingsExperimentalAboutPadding;
	const auto button = Ui::CreateChild<Button>(
		container.get(),
		rpl::single(QString()),
		st);
	const auto title = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			name,
			st::settingsExperimentalTitle),
		titlePadding);
	const auto about = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			description,
			st::settingsExperimentalAbout),
		aboutPadding);
	title->setAttribute(Qt::WA_TransparentForMouseEvents);
	about->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		container->widthValue(),
		title->heightValue(),
		about->heightValue()
	) | rpl::on_next([=](int width, int titleHeight, int aboutHeight) {
		button->resize(width, titlePadding.top()
			+ titleHeight
			+ titlePadding.bottom()
			+ aboutPadding.top()
			+ aboutHeight
			+ aboutPadding.bottom());
	}, button->lifetime());
	title->topValue(
	) | rpl::on_next([=](int top) {
		button->moveToLeft(0, top - titlePadding.top());
	}, button->lifetime());
	button->show();
	return button;
}

QString AddOption(
		not_null<Window::Controller*> window,
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		base::options::option<bool> &option,
		rpl::producer<> resetClicks,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto name = option.name().isEmpty() ? option.id() : option.name();
	const auto text = LocalizedOptionText(
		option.id(),
		name,
		option.description());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();

	auto &lifetime = inner->lifetime();
	const auto toggles = lifetime.make_state<rpl::event_stream<bool>>();
	std::move(
		resetClicks
	) | rpl::map_to(
		option.defaultValue()
	) | rpl::start_to_stream(*toggles, lifetime);
	std::move(reloadOptionsRequests) | rpl::on_next([=, &option] {
		toggles->fire_copy(option.value());
	}, lifetime);

	const auto referrer = OptionReferrer(option);
	const auto button = AddOptionRow(
		inner,
		text.name,
		text.description,
		(!referrer.isEmpty() || option.relevant())
			? st::settingsButtonNoIcon
			: st::settingsOptionDisabled);
	if (!referrer.isEmpty()) {
		button->addClickHandler([=] {
			const auto resolved = ResolveReferrer(
				referrer,
				&controller->session());
			controller->setHighlightControlId(resolved.controlId);
			controller->showSettings(resolved.section);
			window->activate();
		});
	} else {
		button->toggleOn(toggles->events_starting_with(option.value()));
	}

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option.id(), button);
	}

	SetupCopyDeepLink(window, button, option.id());

	const auto restarter = (referrer.isEmpty()
		&& option.relevant()
		&& option.restartRequired())
		? button->lifetime().make_state<base::Timer>()
		: nullptr;
	if (restarter) {
		restarter->setCallback([=] {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		});
	}
	if (referrer.isEmpty()) {
		button->toggledChanges(
		) | rpl::on_next([=, &option](bool toggled) {
			if (!option.relevant() && toggled != option.defaultValue()) {
				toggles->fire_copy(option.defaultValue());
				window->showToast(
					tr::lng_settings_experimental_irrelevant(tr::now));
				return;
			}
			option.set(toggled);
			if (restarter) {
				restarter->callOnce(st::settingsButtonNoIcon.toggle.duration);
			}
		}, inner->lifetime());
	}

	const auto searchable = text.name + ' ' + text.description;
	const auto terms = SearchWords(searchable);
	std::move(
		query
	) | rpl::on_next([=](const QString &text) {
		wrap->toggle(
			MatchesWords(terms, SearchWords(text)),
			anim::type::instant);
	}, wrap->lifetime());

	return searchable;
}

QString AddFavoriteLinkButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto option = &base::options::lookup<QString>(
		Window::kOptionFolderFavoriteLink);
	const auto name = option->name().isEmpty()
		? option->id()
		: option->name();
	const auto text = LocalizedOptionText(
		option->id(),
		name,
		option->description());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();

	auto label = rpl::single(
		rpl::empty
	) | rpl::then(
		option->changes()
	) | rpl::map([option] {
		return option->value();
	});
	const auto button = AddButtonWithLabel(
		inner,
		rpl::single(text.name),
		std::move(label),
		st::settingsButtonNoIcon);
	button->setClickedCallback([=] {
		window->show(Box(Window::EditFolderFavoriteLinkBox));
	});

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option->id(), button);
	}

	SetupCopyDeepLink(window, button, option->id());

	const auto searchable = text.name + ' ' + text.description;
	const auto terms = SearchWords(searchable);
	std::move(
		query
	) | rpl::on_next([=](const QString &text) {
		wrap->toggle(
			MatchesWords(terms, SearchWords(text)),
			anim::type::instant);
	}, wrap->lifetime());

	return searchable;
}

void SetupExperimental(
		not_null<Window::Controller*> window,
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto headerWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto header = headerWrap->entity();

	Ui::AddSkip(header, st::settingsCheckboxesSkip);

	header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::defaultBoxDividerLabelPadding);

	auto reset = (Button*)nullptr;
	if (base::options::changed()) {
		const auto wrap = header->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				header,
				object_ptr<Ui::VerticalLayout>(header)));
		const auto inner = wrap->entity();
		Ui::AddDivider(inner);
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
		reset = inner->add(object_ptr<Button>(
			inner,
			tr::lng_settings_experimental_restore(),
			st::settingsButtonNoIcon));
		reset->addClickHandler([=] {
			base::options::reset();
			wrap->hide(anim::type::normal);
		});
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
	}

	Ui::AddDivider(header);

	rpl::duplicate(
		query
	) | rpl::on_next([=](const QString &text) {
		headerWrap->toggle(text.trimmed().isEmpty(), anim::type::instant);
	}, headerWrap->lifetime());

	struct Category {
		QString title;
		std::vector<const char*> options;
	};
	const auto categories = std::vector<Category>{
		{
			tr::lng_settings_experimental_category_chats(tr::now),
			{
				Dialogs::kOptionForumHideChatsList,
				Dialogs::kOptionDialogsUnreadOnTop,
				Dialogs::Ui::kOptionDialogsMuteIcon,
				kOptionUseNewChatView,
				kOptionAutoScrollInactiveChat,
				kModerateCommonGroups,
				Info::kClassicProfileScroll,
			}
		},
		{
			tr::lng_settings_experimental_category_messages(tr::now),
			{
				Ui::kOptionUseSmallMsgBubbleRadius,
				HistoryView::kOptionUnlimitedMessageWidth,
				HistoryView::Controls::kOptionMacCmdReplyImmediately,
				Ui::kOptionHideAiButton,
				kForceComposeSearchOneColumn,
			}
		},
		{
			tr::lng_settings_experimental_category_profile(tr::now),
			{
				Window::kOptionViewProfileInChatsListContextMenu,
				Info::Profile::kOptionShowPeerIdBelowAbout,
				Info::Profile::kOptionShowChannelJoinedBelowAbout,
				Info::Profile::kOptionProfileMediaTabs,
				Info::Profile::kOptionProfileMediaTabsExpanded,
			}
		},
		{
			tr::lng_settings_experimental_category_stickers_emoji(tr::now),
			{
				ChatHelpers::kOptionTabbedPanelShowOnClick,
				ChatHelpers::kOptionUnlimitedRecentStickers,
			}
		},
		{
			tr::lng_settings_experimental_category_media(tr::now),
			{
				Media::Player::kOptionDisableAutoplayNext,
				Window::kOptionExternalMediaViewer,
				FFmpeg::kOptionFFmpegMultiThread,
			}
		},
		{
			tr::lng_settings_experimental_category_notifications(tr::now),
			{
				Window::Notifications::kOptionHideReplyButton,
				Window::Notifications::kOptionCustomNotification,
				Window::Notifications::kOptionGNotification,
				Window::Notifications::kOptionMacModernNotifications,
			}
		},
		{
			tr::lng_settings_experimental_category_interface(tr::now),
			{
				Core::kOptionFractionalScalingEnabled,
				Core::kOptionHighDpiDownscale,
				Ui::GL::kOptionUseQtRhi,
				Ui::GL::kOptionEnableVulkanRhi,
				Core::kOptionFreeType,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
				Ui::kOptionKineticScroller,
#endif
				Window::kOptionDisableTouchbar,
				Window::kOptionNewWindowsSizeAsFirst,
			}
		},
		{
			tr::lng_settings_experimental_category_system(tr::now),
			{
				MTP::details::kOptionPreferIPv6,
				Core::kOptionSkipUrlSchemeRegister,
				Core::kOptionDeadlockDetector,
				Webview::kOptionWebviewDebugEnabled,
				Webview::kOptionWebviewLegacyEdge,
			}
		},
	};

	const auto addOption = [&](
			not_null<Ui::VerticalLayout*> inner,
			const char name[]) {
		return AddOption(
			window,
			controller,
			inner,
			base::options::lookup<bool>(name),
			(reset
				? (reset->clicks() | rpl::to_empty)
				: rpl::producer<>()),
			rpl::duplicate(reloadOptionsRequests),
			rpl::duplicate(query),
			registerHighlight);
	};
	const auto addCategory = [&](
			const QString &title,
			auto &&fill) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, rpl::single(title));
		auto searchable = std::vector<QString>();
		fill(inner, searchable);
		Ui::AddSkip(inner);
		Ui::AddDivider(inner);

		auto terms = std::vector<QStringList>();
		for (const auto &entry : searchable) {
			terms.push_back(SearchWords(entry));
		}
		rpl::duplicate(
			query
		) | rpl::on_next([=](const QString &text) {
			const auto words = SearchWords(text);
			const auto matches = words.isEmpty()
				|| ranges::any_of(terms, [&](const QStringList &entry) {
					return MatchesWords(entry, words);
				});
			wrap->toggle(matches, anim::type::instant);
		}, wrap->lifetime());
	};

	for (const auto &category : categories) {
		addCategory(category.title, [&](
				not_null<Ui::VerticalLayout*> inner,
				std::vector<QString> &searchable) {
			for (const auto name : category.options) {
				searchable.push_back(addOption(inner, name));
			}
		});
	}

	addCategory(tr::lng_settings_experimental_category_other(tr::now), [&](
			not_null<Ui::VerticalLayout*> inner,
			std::vector<QString> &searchable) {
		if (base::options::lookup<bool>(kOptionFastButtonsMode).value()) {
			searchable.push_back(addOption(inner, kOptionFastButtonsMode));
		}
		searchable.push_back(AddFavoriteLinkButton(
			window,
			inner,
			rpl::duplicate(query),
			registerHighlight));
	});
}

} // namespace

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

Experimental::~Experimental() = default;

rpl::producer<QString> Experimental::title() {
	return tr::lng_settings_experimental();
}

void Experimental::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto window = &controller()->window();
	addAction(
		tr::lng_settings_experimental_export(tr::now),
		[=] {
			TextUtilities::SetClipboardText(
				{ EncodeOptionsToText(base::options::serialize()) });
			window->showToast({
				.text = { tr::lng_settings_experimental_code_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		},
		&st::menuIconCopy);
	if (!DecodeOptionsFromText(QGuiApplication::clipboard()->text()).ok) {
		return;
	}
	addAction(
		tr::lng_settings_experimental_import(tr::now),
		[=] {
			const auto decoded = DecodeOptionsFromText(
				QGuiApplication::clipboard()->text());
			if (!decoded.ok) {
				window->showToast(
					tr::lng_settings_experimental_clipboard_invalid(tr::now));
				return;
			}
			if (!base::options::deserialize(decoded.json)) {
				window->showToast(
					tr::lng_settings_experimental_unsupported_format(tr::now));
				return;
			}
			_reloadOptionsRequests.fire({});
			window->showToast(tr::lng_settings_experimental_imported(tr::now));
		},
		&st::menuIconImportTheme);
}

void Experimental::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	} else {
		setFocus();
	}
}

void Experimental::showFinished() {
	AbstractSection::showFinished();
	for (const auto &[id, widget] : _highlights) {
		if (widget) {
			controller()->checkHighlightControl(id, widget);
		}
	}
}

base::weak_qptr<Ui::RpWidget> Experimental::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto search = CreateSectionSearchRow(parent, _query.current());
	_searchController = std::move(search.controller);
	const auto row = search.row;
	_searchField = search.field;

	_searchController->queryValue(
	) | rpl::on_next([=](QString text) {
		_query = std::move(text);
	}, row->lifetime());

	return base::make_weak(row);
}

void Experimental::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupExperimental(
		&controller()->window(),
		controller(),
		content,
		_reloadOptionsRequests.events(),
		_query.value(),
		[this](const QString &id, not_null<QWidget*> widget) {
			_highlights.push_back({ id, widget.get() });
		});

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
