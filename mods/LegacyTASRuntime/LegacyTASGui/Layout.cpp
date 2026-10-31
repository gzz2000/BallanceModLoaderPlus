// Derived from Gamepiaynmo/BML-Mods TASSupport; see PROVENANCE.md.
#include "Layout.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
namespace LegacyTASGui {
Layout::~Layout() {
    delete m_keysGui;delete m_tasListGui;delete m_tasEntryGui;
}
void Layout::Initialize() {
    if (m_tasEntryGui) throw std::logic_error("TAS layout already initialized");
		m_tasEntryGui = new LegacyBGui::Gui(m_host.GetGuiRuntime());
		m_tasEntry = m_tasEntryGui->AddSmallButton("M_Enter_TAS_Records", "TAS", 0.88f, 0.61f, [this]() {
			if (m_host.OpenProjectMenu()) return;
			m_host.LeaveStartMenu();
			m_tasListGui->SetVisible(true);
			});
		m_tasEntry->SetActive(false);
		m_tasListGui = new GuiTASList(m_host);

		m_keysGui = new LegacyBGui::Gui(m_host.GetGuiRuntime());
		m_keysGui->AddPanel("M_TAS_Keys_Bg", VxColor(0, 0, 0, 180), 0.28f, 0.74f, 0.45f, 0.12f);
		m_butUp = m_keysGui->AddSmallButton("M_TAS_Keys_Up", "^", 0.76f, 0.56f);
		m_butDown = m_keysGui->AddSmallButton("M_TAS_Keys_Down", "v", 0.8f, 0.56f);
		m_butLeft = m_keysGui->AddSmallButton("M_TAS_Keys_Left", "<", 0.8f, 0.48f);
		m_butRight = m_keysGui->AddSmallButton("M_TAS_Keys_Right", ">", 0.8f, 0.64f);
		m_butShift = m_keysGui->AddSmallButton("M_TAS_Keys_Shift", "Shift", 0.8f, 0.30f);
		m_butSpace = m_keysGui->AddSmallButton("M_TAS_Keys_Space", "Space", 0.8f, 0.38f);
		m_butQ = m_keysGui->AddSmallButton("M_TAS_Keys_Q", "Q", 0.76f, 0.38f);
		m_butEsc = m_keysGui->AddSmallButton("M_TAS_Keys_Esc", "Esc", 0.76f, 0.30f);
		m_frameCountLabel = m_keysGui->AddTextLabel("M_TAS_FrameCount", "", LegacyExecuteBB::GAMEFONT_01, 0.0f, 0.7f, 1.0f, 0.0353f);
		m_frameCountLabel->SetAlignment(ALIGN_CENTER);
		m_butUp->SetActive(false);
		m_butDown->SetActive(false);
		m_butLeft->SetActive(false);
		m_butRight->SetActive(false);
		m_butShift->SetActive(false);
		m_butSpace->SetActive(false);
		m_butQ->SetActive(false);
		m_butEsc->SetActive(false);
		m_keysGui->SetVisible(false);
}
void Layout::ProcessMenu(CK2dEntity *levelButton) {
    if(m_tasEntryGui && levelButton) {
        m_tasEntryGui->Process();
        m_tasEntry->SetVisible(levelButton->IsVisible());
        if(m_tasListGui->IsVisible())m_tasListGui->Process();
    }
}
void Layout::UpdateKeys(unsigned keyMask,int frame,bool visible) {
    if(!m_keysGui)return;
    m_keysGui->SetVisible(visible);
    if(!visible)return;
    LegacyBGui::Button *buttons[]={m_butUp,m_butDown,m_butLeft,m_butRight,m_butShift,m_butSpace,m_butQ,m_butEsc};
    for(unsigned i=0;i<8;++i) {
        if(keyMask&(1u<<i))buttons[i]->OnMouseEnter();else buttons[i]->OnMouseLeave();
        buttons[i]->Process();
    }
    char text[100];std::snprintf(text,sizeof(text),"#%d",frame);
    m_frameCountLabel->SetText(text);m_frameCountLabel->Process();
}
GuiTASList::GuiTASList(Host &host):LegacyBGui::Gui(host.GetGuiRuntime()),m_host(host) {
	m_left = AddLeftButton("M_List_Left", 0.4f, 0.34f, [this]() { PreviousPage(); });
	m_right = AddRightButton("M_List_Right", 0.4f, 0.6238f, [this]() { NextPage(); });
	AddBackButton("M_Opt_Mods_Back")->SetCallback([this]() { Exit(); });
	AddTextLabel("M_Opt_Mods_Title", "TAS Records", LegacyExecuteBB::GAMEFONT_02, 0.35f, 0.07f, 0.3f, 0.05f);

	Init(0, 12);
	SetVisible(false);
}

void GuiTASList::Init(int size, int maxsize) {
	m_size = size;
	m_maxpage = (size + maxsize - 1) / maxsize;
	m_maxsize = maxsize;
	m_curpage = 0;

	for (int i = 0; i < m_maxsize; i++) {
		LegacyBGui::Button* button = CreateButton(i);
		m_buttons.push_back(button);
		LegacyBGui::Text* text = AddText(("M_TAS_Text_" + std::to_string(i)).c_str(), "", 0.44f, 0.15f + 0.057f * i, 0.3f, 0.05f);
		text->SetZOrder(100);
		m_texts.push_back(text);
	}
}

void GuiTASList::Resize(int size) {
	m_size = size;
	m_maxpage = (size + m_maxsize - 1) / m_maxsize;
}

void GuiTASList::RefreshRecords() {

	m_records.clear();
	const auto root = m_host.RecordDirectory();
	if (root.empty()) return;
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
	     !ec && it != end; it.increment(ec)) {
		const auto &f = *it;
		std::error_code fileError;
		if (f.is_regular_file(fileError)) {
			std::string ext = f.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".tas") {
				TASInfo info;
				try {
					info.displayname = f.path().stem().string();
					info.filepath = f.path().string();
				}
				catch (std::system_error e) {}

				m_records.push_back(info);
			}
		}
	}

	std::sort(m_records.begin(), m_records.end());
}

void GuiTASList::SetPage(int page) {
	int size = (std::min)(m_maxsize, m_size - page * m_maxsize);
	for (int i = 0; i < m_maxsize; i++) {
		m_buttons[i]->SetVisible(i < size);
		m_texts[i]->SetVisible(i < size);
	}
	for (int i = 0; i < size; i++)
		m_texts[i]->SetText(GetButtonText(page * m_maxsize + i).c_str());

	m_curpage = page;
	m_left->SetVisible(page > 0);
	m_right->SetVisible(page < m_maxpage - 1);
}

void GuiTASList::SetVisible(bool visible) {
	Gui::SetVisible(visible);
	m_visible = visible;
	if (visible) {
		RefreshRecords();
		Resize(m_records.size());
		SetPage((std::max)((std::min)(m_curpage, m_maxpage - 1), 0));
	}
}

LegacyBGui::Button* GuiTASList::CreateButton(int index) {
	return AddLevelButton(("M_TAS_But_" + std::to_string(index)).c_str(), "", 0.15f + 0.057f * index, 0.4031f, [this, index]() {
		GuiTASList::Exit();
		TASInfo& info = m_records[m_curpage * m_maxsize + index];
		m_host.GetBML()->SendIngameMessage(("Loading TAS Record: " + info.displayname).c_str());
		m_host.LoadTAS(info.filepath);
		});
}

const std::string GuiTASList::GetButtonText(int index) {
	return m_records[index].displayname;
}

void GuiTASList::Exit() {
	m_host.GetBML()->GetCKContext()->GetCurrentScene()->Activate(
		m_host.GetBML()->GetScriptByName("Menu_Main"), true);
	SetVisible(false);
}

} // namespace LegacyTASGui
