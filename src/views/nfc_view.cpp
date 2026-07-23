#include "views/nfc_view.hpp"

#include "views/reader_info_panel.hpp"
#include "views/scan_panel.hpp"
#include "views/tag_detail_panel.hpp"
#include "views/ui_primitives.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <core/easing/ease.hpp>
#include <spdlog/spdlog.h>

#include <cmath>
#include <utility>

namespace cap_nfc {
namespace {

constexpr float kPageFadeDuration = 0.15F;

void configureFade(smooth_ui_toolkit::AnimateValue& opacity)
{
    opacity.easingOptions().duration       = kPageFadeDuration;
    opacity.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_in_out_quad;
}

}  // namespace

class NfcView::NfcPager {
public:
    NfcPager(lv_obj_t* parent, NfcViewModel& viewModel, bool showTitle)
        : _scan(std::make_unique<ScanPanel>(parent)),
          _detail(std::make_unique<TagDetailPanel>(parent)),
          _reader_info(std::make_unique<ReaderInfoPanel>(parent)),
          _indicator(std::make_unique<ui::PageIndicator>(parent)),
          _title(std::make_unique<ui::TitleHud>(parent)),
          _initialization_dialog(std::make_unique<ui::InitializationDialog>(
              parent, [&viewModel]() { viewModel.dismissInitializationDialog(); },
              [&viewModel]() { viewModel.retryReader(); })),
          _scan_opacity(255),
          _detail_opacity(0),
          _info_opacity(0)
    {
        configureFade(_scan_opacity);
        configureFade(_detail_opacity);
        configureFade(_info_opacity);
        _scan->setOpacity(LV_OPA_COVER);
        _scan->setHidden(false);
        _detail->setOpacity(LV_OPA_TRANSP);
        _detail->setHidden(true);
        _reader_info->setOpacity(LV_OPA_TRANSP);
        _reader_info->setHidden(true);
        if (showTitle) {
            _title->show();
        }
    }

    void setReaderStatus(const nfc::ReaderStatus& status)
    {
        _scan->setReaderStatus(status);
        _reader_info->setReaderStatus(status);
        _initialization_dialog->setBackendUnavailable(status.backendUnavailable);
        _initialization_dialog->setRetryEnabled(status.state == nfc::ReaderState::Error && !status.ready &&
                                                !status.backendUnavailable);
    }

    void setTagSession(const std::optional<nfc::TagSession>& session)
    {
        _scan->setTagSession(session);
        _detail->setTagSession(session);
    }

    void setSection(NfcSection section)
    {
        if (!_section_initialized) {
            _section_initialized = true;
            _section             = section;
            _scan_opacity.teleport(section == NfcSection::Scan ? 255.0F : 0.0F);
            _detail_opacity.teleport(section == NfcSection::TagDetail ? 255.0F : 0.0F);
            _info_opacity.teleport(section == NfcSection::ReaderInfo ? 255.0F : 0.0F);
            applyOpacity();
            _scan->setHidden(section != NfcSection::Scan);
            _detail->setHidden(section != NfcSection::TagDetail);
            _reader_info->setHidden(section != NfcSection::ReaderInfo);
            _indicator->setSection(section);
            return;
        }

        if (section == _section || section == NfcSection::Count) {
            return;
        }

        _scan_opacity.update();
        _detail_opacity.update();
        _info_opacity.update();
        applyOpacity();
        _section = section;
        if (_section != NfcSection::Scan) {
            _title->dismiss();
        }
        _scan->setHidden(false);
        _detail->setHidden(false);
        _reader_info->setHidden(false);
        _scan_opacity.move(section == NfcSection::Scan ? 255.0F : 0.0F);
        _detail_opacity.move(section == NfcSection::TagDetail ? 255.0F : 0.0F);
        _info_opacity.move(section == NfcSection::ReaderInfo ? 255.0F : 0.0F);
        _indicator->setSection(section);
    }

    void scroll(int32_t amount)
    {
        if (_section == NfcSection::TagDetail) {
            _detail->scrollBy(amount);
        } else if (_section == NfcSection::ReaderInfo) {
            _reader_info->scrollBy(amount);
        }
    }

    void setInitializationDialogActive(bool active)
    {
        _dialog_active = active;
        if (active) {
            _title->dismiss();
            _indicator->setHidden(true);
            setScrollbarsHidden(true);
        }
        _initialization_dialog->setActive(active);
    }

    void tick(uint32_t nowMs)
    {
        _scan->tick(nowMs);
        _detail->tick(nowMs);
        _reader_info->tick(nowMs);
        _title->tick(nowMs);
        _initialization_dialog->tick(nowMs);

        const float nowSeconds = static_cast<float>(nowMs) / 1000.0F;
        _scan_opacity.update(nowSeconds);
        _detail_opacity.update(nowSeconds);
        _info_opacity.update(nowSeconds);
        applyOpacity();

        if (!_dialog_active && _initialization_dialog->hidden()) {
            _indicator->setHidden(false);
            setScrollbarsHidden(false);
        }
        if (_section != NfcSection::Scan && _scan_opacity.done() && _scan_opacity.directValue() <= 0.0F) {
            _scan->setHidden(true);
        }
        if (_section != NfcSection::TagDetail && _detail_opacity.done() && _detail_opacity.directValue() <= 0.0F) {
            _detail->setHidden(true);
        }
        if (_section != NfcSection::ReaderInfo && _info_opacity.done() && _info_opacity.directValue() <= 0.0F) {
            _reader_info->setHidden(true);
        }
    }

    bool initializationDialogHidden() const
    {
        return _initialization_dialog->hidden();
    }

private:
    std::unique_ptr<ScanPanel> _scan;
    std::unique_ptr<TagDetailPanel> _detail;
    std::unique_ptr<ReaderInfoPanel> _reader_info;
    std::unique_ptr<ui::PageIndicator> _indicator;
    std::unique_ptr<ui::TitleHud> _title;
    std::unique_ptr<ui::InitializationDialog> _initialization_dialog;
    smooth_ui_toolkit::AnimateValue _scan_opacity;
    smooth_ui_toolkit::AnimateValue _detail_opacity;
    smooth_ui_toolkit::AnimateValue _info_opacity;
    NfcSection _section       = NfcSection::Scan;
    bool _section_initialized = false;
    bool _dialog_active       = false;

    void applyOpacity()
    {
        _scan->setOpacity(ui::toOpacity(_scan_opacity.directValue()));
        _detail->setOpacity(ui::toOpacity(_detail_opacity.directValue()));
        _reader_info->setOpacity(ui::toOpacity(_info_opacity.directValue()));
    }

    void setScrollbarsHidden(bool hidden)
    {
        _detail->setScrollbarHidden(hidden);
        _reader_info->setScrollbarHidden(hidden);
    }
};

NfcView::NfcView(NfcViewModel& viewModel) : _view_model(viewModel)
{
}

NfcView::~NfcView()
{
    onExit();
}

void NfcView::onEnter(lv_obj_t* parent)
{
    onExit();

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(320, 170);
    _root->setPos(0, 0);
    _root->setBgColor(lv_color_hex(0x0B0C0E));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setShadowWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    const bool showTitle = !_title_shown;
    _title_shown         = true;
    _pager               = std::make_unique<NfcPager>(_root->raw_ptr(), _view_model, showTitle);
    _scroll_serial_seen  = _view_model.scrollRequest().get().serial;

    _view_model.readerStatus().observe(this, onReaderStatusChanged);
    _view_model.tagSession().observe(this, onTagSessionChanged);
    _view_model.section().observe(this, onSectionChanged);
    _view_model.scrollRequest().observe(this, onScrollRequestChanged);
    _view_model.initializationDialogActive().observe(this, onInitializationDialogActiveChanged);
    spdlog::info("NfcView: enter");
}

void NfcView::onExit()
{
    _view_model.initializationDialogActive().removeObserver();
    _view_model.scrollRequest().removeObserver();
    _view_model.section().removeObserver();
    _view_model.tagSession().removeObserver();
    _view_model.readerStatus().removeObserver();
    _pager.reset();
    _root.reset();
}

void NfcView::tick(uint32_t nowMs)
{
    if (_pager) {
        _pager->tick(nowMs);
        if (!_view_model.initializationDialogActive().get() && _pager->initializationDialogHidden()) {
            _view_model.notifyInitializationDialogHidden();
        }
    }
}

void NfcView::onReaderStatusChanged(void* context, const nfc::ReaderStatus& status)
{
    auto* self = static_cast<NfcView*>(context);
    if (self && self->_pager) {
        self->_pager->setReaderStatus(status);
    }
}

void NfcView::onTagSessionChanged(void* context, const std::optional<nfc::TagSession>& session)
{
    auto* self = static_cast<NfcView*>(context);
    if (self && self->_pager) {
        self->_pager->setTagSession(session);
    }
}

void NfcView::onSectionChanged(void* context, const NfcSection& section)
{
    auto* self = static_cast<NfcView*>(context);
    if (self && self->_pager) {
        self->_pager->setSection(section);
    }
}

void NfcView::onScrollRequestChanged(void* context, const ScrollRequest& request)
{
    auto* self = static_cast<NfcView*>(context);
    if (!self || !self->_pager || request.serial == 0 || request.serial == self->_scroll_serial_seen) {
        return;
    }
    self->_scroll_serial_seen = request.serial;
    self->_pager->scroll(request.amount);
}

void NfcView::onInitializationDialogActiveChanged(void* context, const bool& active)
{
    auto* self = static_cast<NfcView*>(context);
    if (self && self->_pager) {
        self->_pager->setInitializationDialogActive(active);
    }
}

}  // namespace cap_nfc
