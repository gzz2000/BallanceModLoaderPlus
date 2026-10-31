// Derived from Gamepiaynmo/BML-Mods TASSupport; see PROVENANCE.md.
#pragma once
#include "../Legacy0340/Gui.h"
#include <filesystem>
namespace LegacyTASGui {
class Host {
public:
    virtual ~Host()=default;
    virtual IBML *GetBML()=0;
    virtual LegacyBGui::Runtime &GetGuiRuntime()=0;
    virtual std::filesystem::path RecordDirectory() const=0;
    virtual void LoadTAS(const std::string &path)=0;
    virtual void LeaveStartMenu()=0;
    // A modern backend may supply its full project/editor UI. Older hosts keep
    // the record-list fallback without changing its construction sequence.
    virtual bool OpenProjectMenu() { return false; }
};
class GuiTASList : public LegacyBGui::Gui {
public:
	explicit GuiTASList(Host &host);
	void Init(int size, int maxsize);
	void Resize(int size);
	void RefreshRecords();

	virtual LegacyBGui::Button* CreateButton(int index);
	virtual const std::string GetButtonText(int index);
	virtual void SetPage(int page);
	virtual void Exit();

	virtual void SetVisible(bool visible) override;
	bool IsVisible() { return m_visible; }

	void PreviousPage() { if (m_curpage > 0) SetPage(m_curpage - 1); }
	void NextPage() { if (m_curpage < m_maxpage - 1) SetPage(m_curpage + 1); }

protected:
    Host &m_host;
	std::vector<LegacyBGui::Button*> m_buttons;
	int m_curpage, m_maxpage, m_size, m_maxsize;
	LegacyBGui::Button* m_left, * m_right;

	struct TASInfo {
		std::string displayname, filepath;
		bool operator <(const TASInfo& o) const {
			return displayname < o.displayname;
		}
	};

	std::vector<TASInfo> m_records;
	std::vector<LegacyBGui::Text*> m_texts;
	bool m_visible;
};


class Layout {
public:
    explicit Layout(Host &host):m_host(host){}
    ~Layout();
    Layout(const Layout&)=delete;
    Layout& operator=(const Layout&)=delete;
    void Initialize();
    void ProcessMenu(CK2dEntity *levelButton);
    void UpdateKeys(unsigned keyMask, int frame, bool visible);
private:
    Host &m_host;
    LegacyBGui::Gui *m_tasEntryGui=nullptr,*m_keysGui=nullptr;
    LegacyBGui::Button *m_tasEntry=nullptr;
    GuiTASList *m_tasListGui=nullptr;
    LegacyBGui::Button *m_butUp,*m_butDown,*m_butLeft,*m_butRight,
        *m_butShift,*m_butSpace,*m_butQ,*m_butEsc;
    LegacyBGui::Label *m_frameCountLabel=nullptr;
};
} // namespace LegacyTASGui
