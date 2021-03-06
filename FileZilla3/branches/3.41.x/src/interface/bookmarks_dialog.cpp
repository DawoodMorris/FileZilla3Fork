#include <filezilla.h>
#include "bookmarks_dialog.h"
#include "filezillaapp.h"
#include "sitemanager.h"
#include "ipcmutex.h"
#include "state.h"
#include "themeprovider.h"
#include "xmlfunctions.h"
#include "xrc_helper.h"

BEGIN_EVENT_TABLE(CNewBookmarkDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CNewBookmarkDialog::OnOK)
EVT_BUTTON(XRCID("ID_BROWSE"), CNewBookmarkDialog::OnBrowse)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CBookmarksDialog, wxDialogEx)
EVT_TREE_SEL_CHANGING(XRCID("ID_TREE"), CBookmarksDialog::OnSelChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_TREE"), CBookmarksDialog::OnSelChanged)
EVT_BUTTON(XRCID("wxID_OK"), CBookmarksDialog::OnOK)
EVT_BUTTON(XRCID("ID_BOOKMARK_BROWSE"), CBookmarksDialog::OnBrowse)
EVT_BUTTON(XRCID("ID_NEW"), CBookmarksDialog::OnNewBookmark)
EVT_BUTTON(XRCID("ID_RENAME"), CBookmarksDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETE"), CBookmarksDialog::OnDelete)
EVT_BUTTON(XRCID("ID_COPY"), CBookmarksDialog::OnCopy)
EVT_TREE_BEGIN_LABEL_EDIT(XRCID("ID_TREE"), CBookmarksDialog::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(XRCID("ID_TREE"), CBookmarksDialog::OnEndLabelEdit)
END_EVENT_TABLE()

CNewBookmarkDialog::CNewBookmarkDialog(wxWindow* parent, std::wstring& site_path, Site const* site)
	: m_parent(parent)
	, m_site_path(site_path)
	, site_(site)
{
}

int CNewBookmarkDialog::Run(const wxString &local_path, const CServerPath &remote_path)
{
	if (!Load(m_parent, _T("ID_NEWBOOKMARK"))) {
		return wxID_CANCEL;
	}

	xrc_call(*this, "ID_LOCALPATH", &wxTextCtrl::ChangeValue, local_path);
	if (!remote_path.empty()) {
		xrc_call(*this, "ID_REMOTEPATH", &wxTextCtrl::ChangeValue, remote_path.GetPath());
	}

	if (!site_) {
		xrc_call(*this, "ID_TYPE_SITE", &wxRadioButton::Enable, false);
	}

	return ShowModal();
}

void CNewBookmarkDialog::OnOK(wxCommandEvent&)
{
	bool const global = xrc_call(*this, "ID_TYPE_GLOBAL", &wxRadioButton::GetValue);

	wxString const name = xrc_call(*this, "ID_NAME", &wxTextCtrl::GetValue);
	if (name.empty()) {
		wxMessageBoxEx(_("You need to enter a name for the bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	wxString const local_path = xrc_call(*this, "ID_LOCALPATH", &wxTextCtrl::GetValue);
	wxString remote_path_raw = xrc_call(*this, "ID_REMOTEPATH", &wxTextCtrl::GetValue);

	CServerPath remote_path;
	if (!remote_path_raw.empty()) {
		if (!global && site_) {
			remote_path.SetType(site_->server.GetType());
		}
		if (!remote_path.SetPath(remote_path_raw.ToStdWstring())) {
			wxMessageBoxEx(_("Could not parse remote path."), _("New bookmark"), wxICON_EXCLAMATION);
			return;
		}
	}

	if (local_path.empty() && remote_path_raw.empty()) {
		wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	bool const sync = xrc_call(*this, "ID_SYNC", &wxCheckBox::GetValue);
	if (sync && (local_path.empty() || remote_path_raw.empty())) {
		wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	bool const comparison = xrc_call(*this, "ID_COMPARISON", &wxCheckBox::GetValue);

	if (!global && site_) {
		std::unique_ptr<Site> site;
		if (!m_site_path.empty()) {
			site = CSiteManager::GetSiteByPath(m_site_path).first;
		}
		if (!site) {
			if (wxMessageBoxEx(_("Site-specific bookmarks require the server to be stored in the Site Manager.\nAdd current connection to the site manager?"), _("New bookmark"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
				return;
			}

			m_site_path = CSiteManager::AddServer(*site_);
			if (m_site_path.empty()) {
				wxMessageBoxEx(_("Could not add connection to Site Manager"), _("New bookmark"), wxICON_EXCLAMATION, this);
				EndModal(wxID_CANCEL);
				return;
			}
		}
		else {
			for (auto const& bookmark : site->m_bookmarks) {
				if (bookmark.m_name == name) {
					wxMessageBoxEx(_("A bookmark with the entered name already exists. Please enter an unused name."), _("New bookmark"), wxICON_EXCLAMATION, this);
					return;
				}
			}
		}

		CSiteManager::AddBookmark(m_site_path, name, local_path, remote_path, sync, comparison);

		EndModal(wxID_OK);
	}
	else {
		if (!CBookmarksDialog::AddBookmark(name, local_path, remote_path, sync, comparison)) {
			return;
		}

		EndModal(wxID_OK);
	}
}

void CNewBookmarkDialog::OnBrowse(wxCommandEvent&)
{
	wxTextCtrl *const pText = XRCCTRL(*this, "ID_LOCALPATH", wxTextCtrl);

	wxDirDialog dlg(this, _("Choose the local directory"), pText->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		pText->ChangeValue(dlg.GetPath());
	}
}

class CBookmarkItemData : public wxTreeItemData
{
public:
	CBookmarkItemData()
	{
	}

	CBookmarkItemData(std::wstring const& local_dir, const CServerPath& remote_dir, bool sync, bool comparison)
		: m_local_dir(local_dir), m_remote_dir(remote_dir), m_sync(sync)
		, m_comparison(comparison)
	{
	}

	std::wstring m_local_dir;
	CServerPath m_remote_dir;
	bool m_sync{};
	bool m_comparison{};
};

CBookmarksDialog::CBookmarksDialog(wxWindow* parent, std::wstring& site_path, Site const* site)
	: m_parent(parent)
	, m_site_path(site_path)
	, site_(site)
	, m_pTree()
{
}

void CBookmarksDialog::LoadGlobalBookmarks()
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString name;
		std::wstring local_dir;
		std::wstring remote_dir_raw;
		CServerPath remote_dir;

		name = GetTextElement(bookmark, "Name");
		if (name.empty()) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				continue;
			}
		}
		if (local_dir.empty() && remote_dir.empty()) {
			continue;
		}

		bool sync;
		if (local_dir.empty() || remote_dir.empty()) {
			sync = false;
		}
		else {
			sync = GetTextElementBool(bookmark, "SyncBrowsing");
		}

		bool const comparison = GetTextElementBool(bookmark, "DirectoryComparison");

		CBookmarkItemData *data = new CBookmarkItemData(local_dir, remote_dir, sync, comparison);
		m_pTree->AppendItem(m_bookmarks_global, name, 1, 1, data);
	}

	m_pTree->SortChildren(m_bookmarks_global);
}

void CBookmarksDialog::LoadSiteSpecificBookmarks()
{
	if (m_site_path.empty()) {
		return;
	}

	auto const site = CSiteManager::GetSiteByPath(m_site_path).first;
	if (!site) {
		return;
	}

	for (auto const& bookmark : site->m_bookmarks) {
		CBookmarkItemData* new_data = new CBookmarkItemData(bookmark.m_localDir, bookmark.m_remoteDir, bookmark.m_sync, bookmark.m_comparison);
		m_pTree->AppendItem(m_bookmarks_site, bookmark.m_name, 1, 1, new_data);
	}

	m_pTree->SortChildren(m_bookmarks_site);
}

int CBookmarksDialog::Run()
{
	if (!Load(m_parent, _T("ID_BOOKMARKS"))) {
		return wxID_CANCEL;
	}

	// Now create the imagelist for the site tree
	m_pTree = XRCCTRL(*this, "ID_TREE", wxTreeCtrl);
	if (!m_pTree) {
		return false;
	}

	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_FOLDER"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));
	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_BOOKMARK"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));

	m_pTree->AssignImageList(pImageList);

	wxTreeItemId root = m_pTree->AddRoot(wxString());
	m_bookmarks_global = m_pTree->AppendItem(root, _("Global bookmarks"), 0, 0);
	LoadGlobalBookmarks();
	m_pTree->Expand(m_bookmarks_global);
	if (site_) {
		m_bookmarks_site = m_pTree->AppendItem(root, _("Site-specific bookmarks"), 0, 0);
		LoadSiteSpecificBookmarks();
		m_pTree->Expand(m_bookmarks_site);
	}

	wxNotebook *pBook = XRCCTRL(*this, "ID_NOTEBOOK", wxNotebook);

	wxPanel* pPanel = new wxPanel;
	wxXmlResource::Get()->LoadPanel(pPanel, pBook, _T("ID_SITEMANAGER_BOOKMARK_PANEL"));
	pBook->AddPage(pPanel, _("Bookmark"));

	xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetContainingSizer)->GetItem((size_t)0)->SetMinSize(200, -1);

	GetSizer()->Fit(this);

	wxSize minSize = GetSizer()->GetMinSize();
	wxSize size = GetSize();
	wxSize clientSize = GetClientSize();
	SetMinSize(GetSizer()->GetMinSize() + size - clientSize);
	SetClientSize(minSize);

	m_pTree->SelectItem(m_bookmarks_global);

	return ShowModal();
}

void CBookmarksDialog::SaveGlobalBookmarks()
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxString msg = file.GetError() + _T("\n\n") + _("The global bookmarks could not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	{
		auto bookmark = element.child("Bookmark");
		while (bookmark) {
			element.remove_child(bookmark);
			bookmark = element.child("Bookmark");
		}
	}

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_pTree->GetFirstChild(m_bookmarks_global, cookie); child.IsOk(); child = m_pTree->GetNextChild(m_bookmarks_global, cookie)) {
		CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(child);
		wxASSERT(data);

		auto bookmark = element.append_child("Bookmark");
		AddTextElement(bookmark, "Name", m_pTree->GetItemText(child).ToStdWstring());
		if (!data->m_local_dir.empty()) {
			AddTextElement(bookmark, "LocalDir", data->m_local_dir);
		}
		if (!data->m_remote_dir.empty()) {
			AddTextElement(bookmark, "RemoteDir", data->m_remote_dir.GetSafePath());
		}
		if (data->m_sync) {
			AddTextElementUtf8(bookmark, "SyncBrowsing", "1");
		}
		if (data->m_comparison) {
			AddTextElementUtf8(bookmark, "DirectoryComparison", "1");
		}
	}

	if (!file.Save(false)) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the global bookmarks could no be saved: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_GLOBALBOOKMARKS);
}

void CBookmarksDialog::SaveSiteSpecificBookmarks()
{
	if (m_site_path.empty()) {
		return;
	}

	if (!CSiteManager::ClearBookmarks(m_site_path)) {
		return;
	}

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_pTree->GetFirstChild(m_bookmarks_site, cookie); child.IsOk(); child = m_pTree->GetNextChild(m_bookmarks_site, cookie)) {
		CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(child);
		wxASSERT(data);

		if (!CSiteManager::AddBookmark(m_site_path, m_pTree->GetItemText(child), data->m_local_dir, data->m_remote_dir, data->m_sync, data->m_comparison)) {
			return;
		}
	}
}

void CBookmarksDialog::OnOK(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateBookmark();

	SaveGlobalBookmarks();
	SaveSiteSpecificBookmarks();

	EndModal(wxID_OK);
}

void CBookmarksDialog::OnBrowse(wxCommandEvent&)
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item) {
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(item);
	if (!data) {
		return;
	}

	wxTextCtrl *pText = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl);

	wxDirDialog dlg(this, _("Choose the local directory"), pText->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		pText->ChangeValue(dlg.GetPath());
	}
}

void CBookmarksDialog::OnSelChanging(wxTreeEvent& event)
{
	if (m_is_deleting) {
		return;
	}

	if (!Verify()) {
		event.Veto();
		return;
	}

	UpdateBookmark();
}

void CBookmarksDialog::OnSelChanged(wxTreeEvent&)
{
	DisplayBookmark();
}

bool CBookmarksDialog::Verify()
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item) {
		return true;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(item);
	if (!data) {
		return true;
	}

	Site const* site;
	if (m_pTree->GetItemParent(item) == m_bookmarks_site) {
		site = site_;
	}
	else {
		site = 0;
	}

	wxString const remotePathRaw = xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue);
	if (!remotePathRaw.empty()) {
		CServerPath remotePath;
		if (site) {
			remotePath.SetType(site->server.GetType());
		}
		if (!remotePath.SetPath(remotePathRaw.ToStdWstring())) {
			xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::SetFocus);
			if (site) {
				wxString msg;
				if (site->server.GetType() != DEFAULT) {
					msg = wxString::Format(_("Remote path cannot be parsed. Make sure it is a valid absolute path and is supported by the current site's servertype (%s)."), CServer::GetNameFromServerType(site->server.GetType()));
				}
				else {
					msg = _("Remote path cannot be parsed. Make sure it is a valid absolute path.");
				}
				wxMessageBoxEx(msg);
			}
			else {
				wxMessageBoxEx(_("Remote path cannot be parsed. Make sure it is a valid absolute path."));
			}
			return false;
		}
	}

	wxString const localPath = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue);

	if (remotePathRaw.empty() && localPath.empty()) {
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."));
		return false;
	}

	bool const sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	if (sync && (localPath.empty() || remotePathRaw.empty())) {
		wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return false;
	}

	return true;
}

void CBookmarksDialog::UpdateBookmark()
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item) {
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(item);
	if (!data) {
		return;
	}

	Site const* site;
	if (m_pTree->GetItemParent(item) == m_bookmarks_site) {
		site = site_;
	}
	else {
		site = 0;
	}

	wxString const remotePathRaw = xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue);
	if (!remotePathRaw.empty()) {
		if (site) {
			data->m_remote_dir.SetType(site->server.GetType());
		}
		data->m_remote_dir.SetPath(remotePathRaw.ToStdWstring());
	}

	data->m_local_dir = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue).ToStdWstring();

	data->m_sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	data->m_comparison = xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::GetValue);
}

void CBookmarksDialog::DisplayBookmark()
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item) {
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_DELETE", &wxButton::Enable, false);
		xrc_call(*this, "ID_RENAME", &wxButton::Enable, false);
		xrc_call(*this, "ID_COPY", &wxButton::Enable, false);
		xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, false);
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)m_pTree->GetItemData(item);
	if (!data) {
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_DELETE", &wxButton::Enable, false);
		xrc_call(*this, "ID_RENAME", &wxButton::Enable, false);
		xrc_call(*this, "ID_COPY", &wxButton::Enable, false);
		xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, false);
		return;
	}

	xrc_call(*this, "ID_DELETE", &wxButton::Enable, true);
	xrc_call(*this, "ID_RENAME", &wxButton::Enable, true);
	xrc_call(*this, "ID_COPY", &wxButton::Enable, true);
	xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, true);

	xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, data->m_remote_dir.GetPath());
	xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, data->m_local_dir);

	xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::SetValue, data->m_sync);
	xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::SetValue, data->m_comparison);
}

void CBookmarksDialog::OnNewBookmark(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateBookmark();

	wxTreeItemId item = m_pTree->GetSelection();
	if (!item) {
		item = m_bookmarks_global;
	}

	if (m_pTree->GetItemData(item)) {
		item = m_pTree->GetItemParent(item);
	}

	if (item == m_bookmarks_site) {

		std::unique_ptr<Site> site;
		if (!m_site_path.empty()) {
			site = CSiteManager::GetSiteByPath(m_site_path).first;
		}

		if (!site) {
			if (wxMessageBoxEx(_("Site-specific bookmarks require the server to be stored in the Site Manager.\nAdd current connection to the site manager?"), _("New bookmark"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
				return;
			}

			m_site_path = CSiteManager::AddServer(*site_);
			if (m_site_path.empty()) {
				wxMessageBoxEx(_("Could not add connection to Site Manager"), _("New bookmark"), wxICON_EXCLAMATION, this);
				return;
			}
		}
	}

	wxString newName = _("New bookmark");
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = m_pTree->GetFirstChild(item, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString name = m_pTree->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = m_pTree->GetNextChild(item, cookie);
		}
		if (!found) {
			break;
		}

		newName = _("New bookmark") + wxString::Format(_T(" %d"), index++);
	}

	wxTreeItemId child = m_pTree->AppendItem(item, newName, 1, 1, new CBookmarkItemData);
	m_pTree->SortChildren(item);
	m_pTree->SelectItem(child);
	m_pTree->EditLabel(child);
}

void CBookmarksDialog::OnRename(wxCommandEvent&)
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		return;
	}

	m_pTree->EditLabel(item);
}

void CBookmarksDialog::OnDelete(wxCommandEvent&)
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		return;
	}

	wxTreeItemId parent = m_pTree->GetItemParent(item);

	m_is_deleting = true;
	m_pTree->Delete(item);
	m_pTree->SelectItem(parent);
	m_is_deleting = false;
}

void CBookmarksDialog::OnCopy(wxCommandEvent&)
{
	wxTreeItemId item = m_pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	if (!Verify()) {
		return;
	}

	CBookmarkItemData* data = static_cast<CBookmarkItemData *>(m_pTree->GetItemData(item));
	if (!data) {
		return;
	}

	UpdateBookmark();

	wxTreeItemId parent = m_pTree->GetItemParent(item);

	const wxString oldName = m_pTree->GetItemText(item);
	wxString newName = wxString::Format(_("Copy of %s"), oldName);
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = m_pTree->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString name = m_pTree->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = m_pTree->GetNextChild(parent, cookie);
		}
		if (!found) {
			break;
		}

		newName = wxString::Format(_("Copy (%d) of %s"), index++, oldName);
	}

	CBookmarkItemData* newData = new CBookmarkItemData(*data);
	wxTreeItemId newItem = m_pTree->AppendItem(parent, newName, 1, 1, newData);

	m_pTree->SortChildren(parent);
	m_pTree->SelectItem(newItem);
	m_pTree->EditLabel(newItem);
}

void CBookmarksDialog::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
	if (item != m_pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		event.Veto();
		return;
	}
}

void CBookmarksDialog::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (item != m_pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		event.Veto();
		return;
	}

	wxString name = event.GetLabel();

	wxTreeItemId parent = m_pTree->GetItemParent(item);

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_pTree->GetFirstChild(parent, cookie); child.IsOk(); child = m_pTree->GetNextChild(parent, cookie)) {
		if (child == item) {
			continue;
		}
		if (!name.CmpNoCase(m_pTree->GetItemText(child))) {
			wxMessageBoxEx(_("Name already exists"), _("Cannot rename entry"), wxICON_EXCLAMATION, this);
			event.Veto();
			return;
		}
	}

	m_pTree->SortChildren(parent);
}

bool CBookmarksDialog::GetGlobalBookmarks(std::vector<std::wstring> &bookmarks)
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring name;
		std::wstring local_dir;
		std::wstring remote_dir_raw;
		CServerPath remote_dir;

		name = GetTextElement(bookmark, "Name");
		if (name.empty()) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				continue;
			}
		}
		if (local_dir.empty() && remote_dir.empty()) {
			continue;
		}

		bookmarks.push_back(name);
	}

	return true;
}

bool CBookmarksDialog::GetBookmark(const wxString &name, wxString &local_dir, CServerPath &remote_dir, bool &sync, bool &comparison)
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring remote_dir_raw;

		if (name != GetTextElement(bookmark, "Name")) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				return false;
			}
		}
		if (local_dir.empty() && remote_dir_raw.empty()) {
			return false;
		}

		if (local_dir.empty() || remote_dir_raw.empty()) {
			sync = false;
		}
		else {
			sync = GetTextElementBool(bookmark, "SyncBrowsing", false);
		}

		comparison = GetTextElementBool(bookmark, "DirectoryComparison", false);
		return true;
	}

	return false;
}


bool CBookmarksDialog::AddBookmark(const wxString &name, const wxString &local_dir, const CServerPath &remote_dir, bool sync, bool comparison)
{
	if (local_dir.empty() && remote_dir.empty()) {
		return false;
	}
	if ((local_dir.empty() || remote_dir.empty()) && sync) {
		return false;
	}

	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxString msg = file.GetError() + _T("\n\n") + _("The bookmark could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	pugi::xml_node bookmark, insertBefore;
	for (bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString remote_dir_raw;

		wxString old_name = GetTextElement(bookmark, "Name");

		if (!name.CmpNoCase(old_name)) {
			wxMessageBoxEx(_("Name of bookmark already exists."), _("New bookmark"), wxICON_EXCLAMATION);
			return false;
		}
		if (name < old_name && !insertBefore) {
			insertBefore = bookmark;
		}
	}

	if (insertBefore) {
		bookmark = element.insert_child_before("Bookmark", insertBefore);
	}
	else {
		bookmark = element.append_child("Bookmark");
	}
	AddTextElement(bookmark, "Name", name.ToStdWstring());
	if (!local_dir.empty()) {
		AddTextElement(bookmark, "LocalDir", local_dir.ToStdWstring());
	}
	if (!remote_dir.empty()) {
		AddTextElement(bookmark, "RemoteDir", remote_dir.GetSafePath());
	}
	if (sync) {
		AddTextElementUtf8(bookmark, "SyncBrowsing", "1");
	}
	if (comparison) {
		AddTextElementUtf8(bookmark, "DirectoryComparison", "1");
	}

	if (!file.Save(false)) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the bookmark could not be added: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		return false;
	}

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_GLOBALBOOKMARKS);

	return true;
}
