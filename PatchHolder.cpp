/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchHolder.h"

#include "Capability.h"

#include "AutomaticCategory.h"
#include "StoredPatchNameCapability.h"
#include "HasBanksCapability.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#include <boost/format.hpp>

#include "RapidjsonHelper.h"
#include "nlohmann/json.hpp"

namespace midikraft {

	const char
		*kFileSource = "filesource",
		*kSynthSource = "synthsource",
		*kBulkSource = "bulksource",
		*kFileInBulk = "fileInBulk",
		*kFileName = "filename",
		*kFullPath = "fullpath",
		*kTimeStamp = "timestamp",
		*kBankNumber = "banknumber",
		*kProgramNo = "program";

	PatchHolder::PatchHolder(std::shared_ptr<Synth> activeSynth, std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<DataFile> patch, 
		MidiBankNumber bank, MidiProgramNumber place, std::shared_ptr<AutomaticCategory> detector /* = nullptr */)
		: sourceInfo_(sourceInfo), patch_(patch), type_(0), isFavorite_(Favorite()), isHidden_(false), synth_(activeSynth), bankNumber_(bank), patchNumber_(place)
	{
		if (patch) {
			name_ = patch->name();
			if (detector) {
				categories_ = detector->determineAutomaticCategories(*this);
			}
		}
	}

	PatchHolder::PatchHolder() : isFavorite_(Favorite()), type_(0), isHidden_(false), bankNumber_(MidiBankNumber::fromZeroBase(0)), patchNumber_(MidiProgramNumber::fromZeroBase(0))
	{
	}

	std::shared_ptr<DataFile> PatchHolder::patch() const
	{
		return patch_;
	}

	midikraft::Synth * PatchHolder::synth() const
	{
		return synth_ ? synth_.get() : nullptr;
	}

	std::shared_ptr<midikraft::Synth> PatchHolder::smartSynth() const
	{
		return synth_;
	}

	int PatchHolder::getType() const
	{
		return patch_->dataTypeID();
	}

	void PatchHolder::setName(std::string const &newName)
	{
		auto storedInPatch = midikraft::Capability::hasCapability<StoredPatchNameCapability>(patch());
		if (storedInPatch) {
			// If the Patch can do it, poke the name into the patch, and then use the result (limited to the characters the synth can do) for the patch holder as well
			storedInPatch->setName(newName);
			name_ = patch()->name();
		}
		else {
			// The name is only stored in the PatchHolder, and thus the database, anyway, so we just accept the string
			name_ = newName;
		}
	}

	std::string PatchHolder::name() const
	{
		return name_;
	}

	void PatchHolder::setSourceId(std::string const &source_id)
	{
		sourceId_ = source_id;
	}

	std::string PatchHolder::sourceId() const
	{
		return sourceId_;
	}

	void PatchHolder::setPatchNumber(MidiProgramNumber number)
	{
		patchNumber_ = number;
	}

	MidiProgramNumber PatchHolder::patchNumber() const
	{
		return patchNumber_;
	}

	void PatchHolder::setBank(MidiBankNumber bank)
	{
		bankNumber_ = bank;
	}

	MidiBankNumber PatchHolder::bankNumber() const
	{
		return bankNumber_;
	}

	bool PatchHolder::isFavorite() const
	{
		return isFavorite_.is() == Favorite::TFavorite::YES;
	}

	Favorite PatchHolder::howFavorite() const
	{
		return isFavorite_;
	}

	void PatchHolder::setFavorite(Favorite fav)
	{
		isFavorite_ = fav;
	}

	void PatchHolder::setSourceInfo(std::shared_ptr<SourceInfo> newSourceInfo)
	{
		sourceInfo_ = newSourceInfo;
	}

	bool PatchHolder::isHidden() const
	{
		return isHidden_;
	}

	void PatchHolder::setHidden(bool isHidden)
	{
		isHidden_ = isHidden;
	}

	bool PatchHolder::hasCategory(Category const &category) const
	{
		return categories_.find(category) != categories_.end();
	}

	void PatchHolder::setCategory(Category const &category, bool hasIt)
	{
		if (!hasIt) {
			if (hasCategory(category)) {
				categories_.erase(category);
			}
		}
		else {
			categories_.insert(category);
		}
	}

	void PatchHolder::setCategories(std::set<Category> const &cats)
	{
		categories_ = cats;
	}

	void PatchHolder::clearCategories()
	{
		categories_.clear();
	}

	std::set<Category> PatchHolder::categories() const
	{
		return categories_;
	}

	std::set<midikraft::Category> PatchHolder::userDecisionSet() const
	{
		return userDecisions_;
	}

	std::shared_ptr<SourceInfo> PatchHolder::sourceInfo() const
	{
		return sourceInfo_;
	}

	bool PatchHolder::autoCategorizeAgain(std::shared_ptr<AutomaticCategory> detector)
	{
		auto previous = categories();
		auto newCategories = detector->determineAutomaticCategories(*this);
		if (previous != newCategories) {
			for (auto n : newCategories) {
				if (userDecisions_.find(n) == userDecisions_.end()) {
					// For this category no user decision has been recorded, so we can safely set it!
					categories_.insert(n);
				}
			}
			for (auto o : previous) {
				if (newCategories.find(o) == newCategories.end()) {
					// This category has been removed by the auto categorizer, let's check if there is no user decision on it!
					if (userDecisions_.find(o) == userDecisions_.end()) {
						categories_.erase(o);
					}
				}
			}
			return previous != categories_;
		}
		else {
			return false;
		}
	}

	std::string PatchHolder::md5() const
	{
		return synth_->calculateFingerprint(patch_);
	}

	std::string PatchHolder::createDragInfoString() const
	{
		// The drag info should be... "PATCH", synth, type, and md5
		nlohmann::json dragInfo = {
			{ "drag_type", "PATCH"},
			{ "synth", synth_->getName() },
			{ "data_type", patch_->dataTypeID()},
			{ "patch_name", patch_->name()},
			{ "md5", md5() }
		};
		return dragInfo.dump(-1, ' ', true, nlohmann::detail::error_handler_t::replace); // Force ASCII, else we get UTF8 exceptions when using some old synths data. Like the MKS50...
	}

	nlohmann::json PatchHolder::dragInfoFromString(std::string s) {
		try {
			return nlohmann::json::parse(s);
		}
		catch (nlohmann::json::parse_error& e) {
			SimpleLogger::instance()->postMessage("Error parsing drop target: " + String(e.what()));
			return {};
		}
	}

	void PatchHolder::setUserDecision(Category const& clicked)
	{
		userDecisions_.insert(clicked);
	}

	void PatchHolder::setUserDecisions(std::set<Category> const &cats)
	{
		userDecisions_ = cats;
	}

	Favorite::Favorite() : favorite_(TFavorite::DONTKNOW)
	{
	}

	Favorite::Favorite(bool isFavorite) : favorite_(isFavorite ? TFavorite::YES : TFavorite::NO)
	{
	}

	Favorite::Favorite(int howFavorite)
	{
		switch (howFavorite) {
		case -1:
			favorite_ = TFavorite::DONTKNOW;
			break;
		case 0:
			favorite_ = TFavorite::NO;
			break;
		case 1:
			favorite_ = TFavorite::YES;
			break;
		default:
			jassert(false);
			favorite_ = TFavorite::DONTKNOW;
		}
	}

	Favorite::TFavorite Favorite::is() const
	{
		return favorite_;
	}

	std::string SourceInfo::toString() const
	{
		return jsonRep_;
	}

	std::shared_ptr<SourceInfo> SourceInfo::fromString(std::string const &str)
	{
		rapidjson::Document doc;
		doc.Parse(str.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kFileSource)) {
				return FromFileSource::fromString(str);
			}
			else if (obj.HasMember(kSynthSource)) {
				return FromSynthSource::fromString(str);
			}
			else if (obj.HasMember(kBulkSource)) {
				return FromBulkImportSource::fromString(str);
			}
		}
		return nullptr;
	}

	bool SourceInfo::isEditBufferImport(std::shared_ptr<SourceInfo> sourceInfo)
	{
		auto synthSource = std::dynamic_pointer_cast<FromSynthSource>(sourceInfo);
		return (synthSource && !synthSource->bankNumber().isValid());
	}

	FromSynthSource::FromSynthSource(Time timestamp, MidiBankNumber bankNo) : timestamp_(timestamp), bankNo_(bankNo)
	{
		rapidjson::Document doc;
		doc.SetObject();
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc.AddMember(rapidjson::StringRef(kSynthSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kTimeStamp), rapidjson::Value(timestring.c_str(), (rapidjson::SizeType) timestring.size()), doc.GetAllocator());
		if (bankNo.isValid()) {
			doc.AddMember(rapidjson::StringRef(kBankNumber), bankNo.toZeroBased(), doc.GetAllocator());
		}
		jsonRep_ = renderToJson(doc);
	}

	FromSynthSource::FromSynthSource(Time timestamp) : FromSynthSource(timestamp, MidiBankNumber::invalid())
	{
	}

	std::string FromSynthSource::md5(Synth *synth) const
	{
		String displayString(toDisplayString(synth, false));
		return MD5(displayString.toUTF8()).toHexString().toStdString();
	}

	std::string FromSynthSource::toDisplayString(Synth *synth, bool shortVersion) const
	{
		ignoreUnused(shortVersion);
		std::string bank = "";
		if (bankNo_.isValid()) {
			auto descriptors = Capability::hasCapability<HasBankDescriptorsCapability>(synth);
			if (descriptors) {
				auto banks = descriptors->bankDescriptors();
				if (bankNo_.toZeroBased() < banks.size()) {
					bank = " " + banks[bankNo_.toZeroBased()].name;
				}
				else {
					bank = (boost::format(" bank %d") % bankNo_.toOneBased()).str();
				}
			}
			else {
				auto bankCapa = Capability::hasCapability<HasBanksCapability>(synth);
				if (bankCapa) {
					bank = " " + bankCapa->friendlyBankName(bankNo_);
				}
				else {
					bank = (boost::format(" bank %d") % bankNo_.toOneBased()).str();;
				}
			}
		}
		else {
			bank = " edit buffer";
		}
		if (timestamp_.toMilliseconds() != 0) {
			// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
			return (boost::format("Imported from synth%s on %s") % bank % timestamp_.formatted("%x at %X").toStdString()).str();
		}
		else {
			// Legacy import, no timestamp was recorded.
			return (boost::format("Imported from synth%s") % bank).str();
		}
	}

	std::shared_ptr<FromSynthSource> FromSynthSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kSynthSource)) {
				Time timestamp;
				if (obj.HasMember(kTimeStamp)) {
					std::string timestring = obj.FindMember(kTimeStamp).operator*().value.GetString();
					timestamp = Time::fromISO8601(timestring);
				}
				MidiBankNumber bankNo = MidiBankNumber::invalid();
				if (obj.HasMember(kBankNumber)) {
					bankNo = MidiBankNumber::fromZeroBase(obj.FindMember(kBankNumber).operator*().value.GetInt());
				}
				return std::make_shared<FromSynthSource>(timestamp, bankNo);
			}
		}
		return nullptr;
	}

	MidiBankNumber FromSynthSource::bankNumber() const
	{
		return bankNo_;
	}

	FromFileSource::FromFileSource(std::string const &filename, std::string const &fullpath, MidiProgramNumber program) : filename_(filename)
	{
		rapidjson::Document doc;
		doc.SetObject();
		doc.AddMember(rapidjson::StringRef(kFileSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kFileName), rapidjson::Value(filename.c_str(), (rapidjson::SizeType)  filename.size()), doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kFullPath), rapidjson::Value(fullpath.c_str(), (rapidjson::SizeType) fullpath.size()), doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kProgramNo), program.toZeroBased(), doc.GetAllocator());
		jsonRep_ = renderToJson(doc);

	}

	std::string FromFileSource::md5(Synth *synth) const
	{
		String displayString(toDisplayString(synth, true));
		return MD5(displayString.toUTF8()).toHexString().toStdString();
	}

	std::string FromFileSource::toDisplayString(Synth *, bool shortVersion) const
	{
		ignoreUnused(shortVersion);
		return (boost::format("Imported from file %s") % filename_).str();
	}

	std::shared_ptr<FromFileSource> FromFileSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kFileSource)) {
				std::string filename = obj.FindMember(kFileName).operator*().value.GetString();
				std::string fullpath = obj.FindMember(kFullPath).operator*().value.GetString();
				MidiProgramNumber program = MidiProgramNumber::fromZeroBase(obj.FindMember(kProgramNo).operator*().value.GetInt());
				return std::make_shared<FromFileSource>(filename, fullpath, program);
			}
		}
		return nullptr;
	}

	FromBulkImportSource::FromBulkImportSource(Time timestamp, std::shared_ptr<SourceInfo> individualInfo) : timestamp_(timestamp), individualInfo_(individualInfo)
	{
		rapidjson::Document doc;
		doc.SetObject();
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc.AddMember(rapidjson::StringRef(kBulkSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kTimeStamp), rapidjson::Value(timestring.c_str(), (rapidjson::SizeType) timestring.size()), doc.GetAllocator());
		if (individualInfo) {
			std::string subinfo = individualInfo->toString();
			doc.AddMember(rapidjson::StringRef(kFileInBulk), rapidjson::Value(subinfo.c_str(), (rapidjson::SizeType)subinfo.size()), doc.GetAllocator());
		} 
		jsonRep_ = renderToJson(doc);
	}

	std::string FromBulkImportSource::md5(Synth *synth) const
	{
		ignoreUnused(synth);
		String uuid((boost::format("Bulk import %s") % timestamp_.formatted("%x at %X")).str());
		return MD5(uuid.toUTF8()).toHexString().toStdString();
	}

	std::string FromBulkImportSource::toDisplayString(Synth *synth, bool shortVersion) const
	{
		if (timestamp_.toMilliseconds() != 0) {
			if (shortVersion || !individualInfo_) {
				// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
				return (boost::format("Bulk import (%s)") % timestamp_.formatted("%x at %X").toStdString()).str();
			}
			else {
				// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
				return (boost::format("Bulk import %s (%s)") % timestamp_.formatted("%x at %X").toStdString() % individualInfo_->toDisplayString(synth, true)).str();
			}
		}
		return "Bulk file import";
	}

	std::shared_ptr<FromBulkImportSource> FromBulkImportSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kBulkSource)) {
				Time timestamp;
				if (obj.HasMember(kTimeStamp)) {
					std::string timestring = obj.FindMember(kTimeStamp).operator*().value.GetString();
					timestamp = Time::fromISO8601(timestring);
				}
				std::shared_ptr<SourceInfo> individualInfo;
				if (obj.HasMember(kFileInBulk)) {
					auto &subinfoJson = obj.FindMember(kFileInBulk).operator*().value;
					if (subinfoJson.IsString()) {
						individualInfo = SourceInfo::fromString(subinfoJson.GetString());
					}
					else {
						std::string subinfo = renderToJson(subinfoJson);
						individualInfo = SourceInfo::fromString(subinfo);
					}
				}
				return std::make_shared<FromBulkImportSource>(timestamp, individualInfo);
			}
		}
		return nullptr;
	}

	std::shared_ptr<SourceInfo> FromBulkImportSource::individualInfo() const
	{
		return individualInfo_;
	}

}
