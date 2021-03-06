/*
 * rcdiscover - the network discovery tool for rc_visard
 *
 * Copyright (c) 2017 Roboception GmbH
 * All rights reserved
 *
 * Author: Raphael Schaller
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// placed here to make sure to include winsock2.h before windows.h
#include "rcdiscover/wol.h"

#include "discover-frame.h"

#include "discover-thread.h"
#include "event-ids.h"
#include "reset-dialog.h"
#include "force-ip-dialog.h"
#include "reconnect-dialog.h"
#include "about-dialog.h"
#include "resources.h"

#include <memory>
#include <sstream>

#include <wx/frame.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/animate.h>
#include <wx/mstream.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/clipbrd.h>
#include <wx/dc.h>
#include <wx/msgdlg.h>
#include <wx/html/helpctrl.h>
#include <wx/cshelp.h>

#include "resources/logo_128.xpm"
#include "resources/logo_32_rotate.h"

DiscoverFrame::DiscoverFrame(const wxString& title,
                const wxPoint& pos) :
  wxFrame(NULL, wxID_ANY, title, pos, wxSize(820,350)),
  device_list_(nullptr),
  discover_button_(nullptr),
  reset_button_(nullptr),
  force_ip_button_(nullptr),
  reset_dialog_(nullptr),
  force_ip_dialog_(nullptr),
  about_dialog_(nullptr),
  menu_event_item_(nullptr),
  only_rc_sensors_(true)
{
  // spinner
  wxIcon icon_128(logo_128_xpm);
  SetIcon(icon_128);

  wxMemoryInputStream gif_stream(logo_32_rotate_gif,
                                 sizeof(logo_32_rotate_gif));
  spinner_.Load(gif_stream, wxANIMATION_TYPE_GIF);

  // menu
  wxMenu *menuFile = new wxMenu();
  menuFile->Append(wxID_EXIT);

  wxMenu *menuHelp = new wxMenu();
  menuHelp->Append(wxID_HELP);
  menuHelp->Append(wxID_ABOUT);

  wxMenuBar *menuBar = new wxMenuBar();
  menuBar->Append(menuFile, "&File");
  menuBar->Append(menuHelp, "&Help");

  SetMenuBar(menuBar);
  CreateStatusBar();

  // window content
  auto *panel = new wxPanel(this, wxID_ANY);
  auto *vbox = new wxBoxSizer(wxVERTICAL);

  // uppper buttons
  {
    auto *button_box = new wxBoxSizer(wxHORIZONTAL);
    discover_button_ = new wxButton(panel, ID_DiscoverButton, "Rerun Discovery");
    button_box->Add(discover_button_, 1);
    button_box->AddSpacer(10);
    int w, h;
    discover_button_->GetSize(&w, &h);
    auto *only_rc_cbox = new wxCheckBox(panel, ID_OnlyRcCheckbox,
                                        "Only Roboception cameras",
                                        wxDefaultPosition, wxSize(-1, h));
    only_rc_cbox->SetValue(only_rc_sensors_);
    button_box->Add(only_rc_cbox, 1);

    button_box->Add(-1, 0, wxEXPAND);

    spinner_ctrl_ = new wxAnimationCtrl(panel, wxID_ANY, spinner_,
                                        wxPoint(-1,-1), wxSize(32,32));
    button_box->Add(spinner_ctrl_, 0);

    vbox->Add(button_box, 0, wxALL, 10);
  }

  // rc_visard table
  {
    auto *data_box = new wxBoxSizer(wxHORIZONTAL);

    device_list_ = new wxDataViewListCtrl(panel,
                                          ID_DataViewListCtrl,
                                          wxPoint(-1,-1),
                                          wxSize(-1,-1));
    device_list_->AppendTextColumn("Name",
                                   wxDATAVIEW_CELL_INERT,
                                   100, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    device_list_->AppendTextColumn("Manufacturer",
                                   wxDATAVIEW_CELL_INERT,
                                   170, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    device_list_->AppendTextColumn("Serial Number",
                                   wxDATAVIEW_CELL_INERT,
                                   130, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    device_list_->AppendTextColumn("IP Address",
                                   wxDATAVIEW_CELL_INERT,
                                   120, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    device_list_->AppendTextColumn("MAC Address",
                                   wxDATAVIEW_CELL_INERT,
                                   130, wxALIGN_LEFT,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    device_list_->AppendTextColumn("Reachable",
                                   wxDATAVIEW_CELL_INERT,
                                   80, wxALIGN_CENTER,
                                   wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

    device_list_->SetToolTip("Double-click row to open WebGUI in browser.");

    data_box->Add(device_list_, 1, wxEXPAND);

    vbox->Add(data_box, 1, wxLEFT | wxRIGHT | wxEXPAND, 10);
  }

  {
    auto *button_box = new wxBoxSizer(wxHORIZONTAL);
    reset_button_ = new wxButton(panel, ID_ResetButton, "Reset rc_visard");
    button_box->Add(reset_button_, 1);
    int w, h;
    reset_button_->GetSize(&w, &h);

    force_ip_button_ = new wxButton(panel, ID_ForceIpButton,
                                    "Set temporary IP address");
    button_box->Add(force_ip_button_, 1);

    reconnect_button_ = new wxButton(panel, ID_ReconnectButton,
                                    "Reconnect rc_visard");
    button_box->Add(reconnect_button_, 1);

    button_box->Add(-1, 0, wxEXPAND);

    auto *help_button = new wxContextHelpButton(panel, ID_Help_Discovery,
                                                wxDefaultPosition, wxSize(h,h));
    button_box->Add(help_button, 0);

    vbox->Add(button_box, 0, wxTOP | wxLEFT | wxRIGHT, 10);
  }

  panel->SetSizer(vbox);
  Centre();

  help_ctrl_ = new wxHtmlHelpController(wxHF_DEFAULT_STYLE, panel);
  help_ctrl_->AddBook("memory:help.hhp");

  Connect(ID_DiscoverButton,
          wxEVT_COMMAND_BUTTON_CLICKED,
          wxCommandEventHandler(DiscoverFrame::onDiscoverButton));
  Connect(wxID_ANY,
          wxEVT_COMMAND_DISCOVERY_COMPLETED,
          wxThreadEventHandler(DiscoverFrame::onDiscoveryCompleted));
  Connect(wxID_ANY,
          wxEVT_COMMAND_DISCOVERY_ERROR,
          wxThreadEventHandler(DiscoverFrame::onDiscoveryError));
  Connect(ID_ResetButton,
          wxEVT_COMMAND_BUTTON_CLICKED,
          wxCommandEventHandler(DiscoverFrame::onResetButton));
  Connect(ID_ForceIpButton,
          wxEVT_COMMAND_BUTTON_CLICKED,
          wxCommandEventHandler(DiscoverFrame::onForceIpButton));
  Connect(ID_ReconnectButton,
          wxEVT_COMMAND_BUTTON_CLICKED,
          wxCommandEventHandler(DiscoverFrame::onReconnectButton));
  Connect(ID_Help_Discovery,
          wxEVT_COMMAND_BUTTON_CLICKED,
          wxCommandEventHandler(DiscoverFrame::onHelpDiscovery));
  Connect(ID_DataViewListCtrl,
          wxEVT_DATAVIEW_ITEM_ACTIVATED,
          wxDataViewEventHandler(DiscoverFrame::onDeviceDoubleClick));
  Connect(ID_DataViewListCtrl,
          wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,
          wxDataViewEventHandler(DiscoverFrame::onDataViewContextMenu));
  Connect(ID_OpenWebGUI,
          wxEVT_MENU,
          wxMenuEventHandler(DiscoverFrame::onOpenWebGUI));
  Connect(wxCOPY,
          wxEVT_MENU,
          wxMenuEventHandler(DiscoverFrame::onCopy));
  Connect(ID_ResetButton,
          wxEVT_MENU,
          wxMenuEventHandler(DiscoverFrame::onResetContextMenu));
  Connect(ID_ForceIpButton,
          wxEVT_MENU,
          wxMenuEventHandler(DiscoverFrame::onForceIpContextMenu));
  Connect(ID_ReconnectButton,
          wxEVT_MENU,
          wxMenuEventHandler(DiscoverFrame::onReconnectContextMenu));
  Connect(wxID_EXIT,
          wxEVT_MENU,
          wxCommandEventHandler(DiscoverFrame::onExit));
  Connect(wxID_ABOUT,
          wxEVT_MENU,
          wxCommandEventHandler(DiscoverFrame::onAbout));
  Connect(wxID_HELP,
          wxEVT_MENU,
          wxCommandEventHandler(DiscoverFrame::onHelp));
  Connect(ID_OnlyRcCheckbox,
          wxEVT_CHECKBOX,
          wxCommandEventHandler(DiscoverFrame::onOnlyRcCheckbox));

  reset_dialog_ = new ResetDialog(help_ctrl_, panel, wxID_ANY);
  force_ip_dialog_ = new ForceIpDialog(help_ctrl_, panel, wxID_ANY);
  reconnect_dialog_ = new ReconnectDialog(help_ctrl_, panel, wxID_ANY);
  about_dialog_ = new AboutDialog(panel, wxID_ANY);

  // start discovery on startup
  wxCommandEvent evt;
  onDiscoverButton(evt);
}

void DiscoverFrame::setBusy()
{
  discover_button_->Disable();
  reset_button_->Disable();
  force_ip_button_->Disable();
  reconnect_button_->Disable();
  spinner_ctrl_->Play();
}

void DiscoverFrame::clearBusy()
{
  discover_button_->Enable();
  reset_button_->Enable();
  force_ip_button_->Enable();
  reconnect_button_->Enable();
  spinner_ctrl_->Stop();

  // on Windows, wxAnimationCtrl is sometimes not stopping even if
  // Stop was called. Calling it multiple times reduces the chance
  // of this happening.
  spinner_ctrl_->Stop();
  spinner_ctrl_->Stop();
}

void DiscoverFrame::onDiscoverButton(wxCommandEvent &)
{
  setBusy();

  auto *thread = new DiscoverThread(this);
  if (thread->Run() != wxTHREAD_NO_ERROR)
  {
    std::cerr << "Could not spawn thread" << std::endl;
    delete thread;
    thread = nullptr;

    clearBusy();
  }
}

void DiscoverFrame::onDiscoveryCompleted(wxThreadEvent &event)
{
  updateDeviceList(event.GetPayload<std::vector<wxVector<wxVariant>>>());

  clearBusy();
}

void DiscoverFrame::updateDeviceList(const std::vector<wxVector<wxVariant>> &d)
{
  device_list_->DeleteAllItems();

  last_data_ = d;
  for(const auto& d : last_data_)
  {
    const auto manufacturer = d[1].GetString();
    if (!only_rc_sensors_ || manufacturer == ROBOCEPTION)
    {
      device_list_->AppendItem(d);
    }
  }

  reset_dialog_->setDiscoveredSensors(device_list_->GetStore());
  force_ip_dialog_->setDiscoveredSensors(device_list_->GetStore());
  reconnect_dialog_->setDiscoveredSensors(device_list_->GetStore());
}

void DiscoverFrame::onDiscoveryError(wxThreadEvent &event)
{
  std::ostringstream oss;
  oss << "An error occurred during discovery: " << event.GetString();
  wxMessageBox(oss.str(), "Error", wxOK | wxICON_ERROR);

  clearBusy();
}

void DiscoverFrame::onResetButton(wxCommandEvent &)
{
  openResetDialog(device_list_->GetSelectedRow());
}

void DiscoverFrame::onForceIpButton(wxCommandEvent &)
{
  openForceIpDialog(device_list_->GetSelectedRow());
}

void DiscoverFrame::onReconnectButton(wxCommandEvent &)
{
  openReconnectDialog(device_list_->GetSelectedRow());
}

void DiscoverFrame::onHelpDiscovery(wxCommandEvent&)
{
  help_ctrl_->Display("help.htm#discovery");
}

void DiscoverFrame::onDeviceDoubleClick(wxDataViewEvent &event)
{
  const auto item = event.GetItem();
  const auto row = device_list_->ItemToRow(item);

  if (row == wxNOT_FOUND)
  {
    return;
  }

  const auto manufacturer = device_list_->GetTextValue(row, 1);
  if (manufacturer == ROBOCEPTION || manufacturer == KUKA)
  {
    const auto ip_wxstring = device_list_->GetTextValue(
                               static_cast<unsigned int>(row), 3);
    wxLaunchDefaultBrowser("http://" + ip_wxstring + "/");
  }
}

void DiscoverFrame::onDataViewContextMenu(wxDataViewEvent &event)
{
  menu_event_item_.reset(new std::pair<int, int>(
                           device_list_->ItemToRow(event.GetItem()),
                           event.GetColumn()));

  if (menu_event_item_->first < 0)
  {
    return;
  }

  wxMenu menu;
  bool appended = false;
  if (menu_event_item_->second >= 0)
  {
    menu.Append(wxCOPY, "&Copy");
    menu.AppendSeparator();
    appended = true;
  }

  const auto manufacturer = device_list_->GetTextValue(
                        static_cast<unsigned int>(menu_event_item_->first),
                        1);
  if (manufacturer == ROBOCEPTION || manufacturer == KUKA)
  {
    menu.Append(ID_OpenWebGUI, "Open &WebGUI");
    menu.AppendSeparator();
    menu.Append(ID_ResetButton, "Reset");
    menu.Append(ID_ForceIpButton, "Set temporary IP");
    menu.Append(ID_ReconnectButton, "Reconnect");
    appended = true;
  }

  if (appended)
  {
    PopupMenu(&menu);
  }
}

void DiscoverFrame::onCopy(wxMenuEvent &)
{
  if (!menu_event_item_ ||
      menu_event_item_->first < 0 ||
      menu_event_item_->second < 0)
  {
    return;
  }

  const auto row = static_cast<unsigned int>(menu_event_item_->first);
  const auto cell = device_list_->GetTextValue(
                      row, static_cast<unsigned int>(menu_event_item_->second));

  if (wxTheClipboard->Open())
  {
    wxTheClipboard->SetData(new wxTextDataObject(cell));
    wxTheClipboard->Close();
  }
}

void DiscoverFrame::onOpenWebGUI(wxMenuEvent &)
{
  if (!menu_event_item_ ||
      menu_event_item_->first < 0)
  {
    return;
  }

  const auto ip_wxstring = device_list_->GetTextValue(
                             static_cast<unsigned int>(menu_event_item_->first),
                             3);
  wxLaunchDefaultBrowser("http://" + ip_wxstring + "/");
}

void DiscoverFrame::onResetContextMenu(wxMenuEvent &)
{
  if (!menu_event_item_ ||
      menu_event_item_->first < 0)
  {
    return;
  }

  openResetDialog(menu_event_item_->first);
}

void DiscoverFrame::onForceIpContextMenu(wxMenuEvent &)
{
  if (!menu_event_item_ ||
      menu_event_item_->first < 0)
  {
    return;
  }

  openForceIpDialog(menu_event_item_->first);
}

void DiscoverFrame::onReconnectContextMenu(wxMenuEvent &)
{
  if (!menu_event_item_ ||
      menu_event_item_->first < 0)
  {
    return;
  }

  openReconnectDialog(menu_event_item_->first);
}

void DiscoverFrame::onExit(wxCommandEvent &)
{
  Close(true);
}

void DiscoverFrame::onHelp(wxCommandEvent&)
{
  help_ctrl_->Display("help.htm");
}

void DiscoverFrame::onOnlyRcCheckbox(wxCommandEvent &evt)
{
  only_rc_sensors_ = evt.IsChecked();
  updateDeviceList(last_data_);
}

void DiscoverFrame::onAbout(wxCommandEvent &)
{
  about_dialog_->ShowModal();
}

void DiscoverFrame::openResetDialog(const int row)
{
  if (row != wxNOT_FOUND)
  {
    reset_dialog_->setActiveSensor(static_cast<unsigned int>(row));
  }

  reset_dialog_->Show();
}

void DiscoverFrame::openForceIpDialog(const int row)
{
  if (row != wxNOT_FOUND)
  {
    force_ip_dialog_->setActiveSensor(static_cast<unsigned int>(row));
  }

  force_ip_dialog_->Show();
}

void DiscoverFrame::openReconnectDialog(const int row)
{
  if (row != wxNOT_FOUND)
  {
    reconnect_dialog_->setActiveSensor(static_cast<unsigned int>(row));
  }

  reconnect_dialog_->Show();
}

BEGIN_EVENT_TABLE(DiscoverFrame, wxFrame)
END_EVENT_TABLE()
