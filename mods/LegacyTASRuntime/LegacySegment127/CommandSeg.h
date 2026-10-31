// Adapted from Swung0x48/Segment 1.2.7 (MIT; see LICENSE).
#pragma once
#include <BML/ICommand.h>

#include "Segment.h"

namespace LegacySegment127 {
class CommandSeg: public ICommand
{
public:
	//CommandSeg(double segmentTime[][9], int* currentLevel);
	CommandSeg(Segment* segment);
	virtual std::string GetName() override { return "segment"; }
	virtual std::string GetAlias() override { return "seg"; }
	virtual std::string GetDescription() override { return "Command for segment."; }
	virtual bool IsCheat() override { return false; }

	virtual void Execute(IBML* bml, const std::vector<std::string>& args) override;
	virtual const std::vector<std::string> GetTabCompletion(IBML* bml, const std::vector<std::string>& args) override {
		return args.size() == 2 ? std::vector<std::string>{ "clear" } : std::vector<std::string>{};
	};
private:
	//double (*_segmentTime)[9];
	//int* _currentLevel;
	Segment* segment_;
};


} // namespace LegacySegment127
