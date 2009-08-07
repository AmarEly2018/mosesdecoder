// $Id$
// vim:tabstop=2

/***********************************************************************
 Moses - factored phrase-based language decoder
 Copyright (C) 2006 University of Edinburgh
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ***********************************************************************/

#include <fstream>
#include <string>
#include <iterator>
#include <algorithm>
#include <sys/stat.h>
#include "PhraseDictionaryNewFormat.h"
#include "FactorCollection.h"
#include "Word.h"
#include "Util.h"
#include "InputFileStream.h"
#include "StaticData.h"
#include "WordsRange.h"
#include "UserMessage.h"
#include "ChartRuleCollection.h"
#include "DotChart.h"
#include "FactorCollection.h"

using namespace std;

namespace Moses
{
	
	inline void TransformString(vector< vector<string>* > &phraseVector)
	{ // for target phrase
		for (size_t pos = 0 ; pos < phraseVector.size() ; ++pos)
		{
			assert(phraseVector[pos]->size() == 1);
			
			string &str = (*phraseVector[pos])[0];
			if (str.substr(0, 1) == "[" && str.substr(str.size()-1, 1) == "]")
			{ // non-term
				str = str.substr(1, str.size() - 2);
			}
		}
	}
	
	inline void TransformString(vector< vector<string>* > &phraseVector
															, vector<string> &sourceLabelsStr)
	{ // for source phrase
		for (size_t pos = 0 ; pos < phraseVector.size() ; ++pos)
		{
			assert(phraseVector[pos]->size() == 1);
			
			string &str = (*phraseVector[pos])[0];
			if (str.substr(0, 1) == "[" && str.substr(str.size()-1, 1) == "]")
			{ // non-term
				string sourceLabel = str.substr(1, str.size() - 2);
				sourceLabelsStr.push_back(sourceLabel);
				str = "X"; // TODO
			}
		}
	}
	
	inline void CreateHeadWord(Word &sourceHeadWord, Word &targetHeadWord, const string &headString
											, const std::vector<FactorType> &input, const std::vector<FactorType> &output
											, const string &factorDelimiter)
	{
		vector<string> vec = Tokenize(headString);
		
		sourceHeadWord.CreateFromString(Input, input,vec[0].substr(1, vec[0].size()-2), true);
		targetHeadWord.CreateFromString(Output, output, vec[1].substr(1, vec[1].size()-2), true);
	}
	
	void CreateAlignmentInfo(list<pair<size_t,size_t> > &alignmentInfo, const string &alignString
													 , const Phrase &sourcePhrase, const Phrase &targetPhrase)
	{
		vector<string> alignVec = Tokenize(alignString);
		
		vector<string>::const_iterator iter;
		for (iter = alignVec.begin(); iter != alignVec.end(); ++iter)
		{
			const string &align1 = *iter;
			vector<size_t> alignPos = Tokenize<size_t>(align1, "-");
			assert(alignPos.size() == 2);
			size_t &sourcePos	= alignPos[0]
						,&targetPos	= alignPos[1];
			
			alignmentInfo.push_back(pair<size_t,size_t>(sourcePos, targetPos));
		}
	}
		
	void PhraseDictionaryNewFormat::CreateSourceLabels(vector<Word> &sourceLabels
																											 , const vector<string> &sourceLabelsStr) const
	{
		FactorCollection &factorCollection = FactorCollection::Instance();
		
		for (size_t ind = 0; ind < sourceLabelsStr.size(); ++ind)
		{
			sourceLabels.push_back(Word());
			Word &word = sourceLabels.back();
			
			// TODO
			const Factor *factor = factorCollection.AddFactor(Input, 0, sourceLabelsStr[ind], true);
			word[0] = factor;
			word.SetIsNonTerminal(true);
		}
	}
	
	bool PhraseDictionaryNewFormat::Load(const std::vector<FactorType> &input
																				 , const std::vector<FactorType> &output
																				 , const string &filePath
																				 , const vector<float> &weight
																				 , size_t tableLimit
																				 , const LMList &languageModels
																				 , float weightWP)
	{
		m_filePath = filePath;
		m_tableLimit = tableLimit;
		
		//factors
		m_inputFactors = FactorMask(input);
		m_outputFactors = FactorMask(output);
		
		// data from file
		InputFileStream inFile(filePath);
				
		bool ret = Load(input, output, inFile, weight, tableLimit, languageModels, weightWP);		
		return ret;
	}
	
	bool PhraseDictionaryNewFormat::Load(const std::vector<FactorType> &input
																				 , const std::vector<FactorType> &output
																				 , std::istream &inStream
																				 , const std::vector<float> &weight
																				 , size_t tableLimit
																				 , const LMList &languageModels
																				 , float weightWP)
	{
		PrintUserTime("Start loading new format pt model");
		
		const StaticData &staticData = StaticData::Instance();
		const std::string& factorDelimiter = staticData.GetFactorDelimiter();
		
		VERBOSE(2,"PhraseDictionaryNewFormat: input=" << m_inputFactors << "  output=" << m_outputFactors << std::endl);
		
		string line;
		size_t count = 0;
		size_t numElement = NOT_FOUND; // 3=old format, 5=async format which include word alignment info
		
		while(getline(inStream, line))
		{
			vector<string> tokens;
			vector<float> scoreVector;
			
			TokenizeMultiCharSeparator(tokens, line , "|||" );
			
			if (numElement == NOT_FOUND)
			{ // init numElement
				numElement = tokens.size();
				assert(numElement == 5);
			}
			
			if (tokens.size() != numElement)
			{
				stringstream strme;
				strme << "Syntax error at " << m_filePath << ":" << count;
				UserMessage::Add(strme.str());
				abort();
			}
			
			const string &headString					= tokens[0]
									, &sourcePhraseString	= tokens[1]
									, &targetPhraseString	= tokens[2]
									, &alignString				= tokens[3]
									, &scoreString				= tokens[4];
			
			bool isLHSEmpty = (sourcePhraseString.find_first_not_of(" \t", 0) == string::npos);
			if (isLHSEmpty && !staticData.IsWordDeletionEnabled()) {
				TRACE_ERR( m_filePath << ":" << count << ": pt entry contains empty target, skipping\n");
				continue;
			}
			
			Tokenize<float>(scoreVector, scoreString);
			if (scoreVector.size() != m_numScoreComponent)
			{
				stringstream strme;
				strme << "Size of scoreVector != number (" <<scoreVector.size() << "!=" <<m_numScoreComponent<<") of score components on line " << count;
				UserMessage::Add(strme.str());
				abort();
			}
			assert(scoreVector.size() == m_numScoreComponent);
			
			// parse source & find pt node
			
			// head word
			Word sourceLHS, targetLHS;
			CreateHeadWord(sourceLHS, targetLHS, headString, input, output, factorDelimiter);

			// source
			Phrase sourcePhrase(Input);
			sourcePhrase.CreateFromStringNewFormat(Input, input, sourcePhraseString, factorDelimiter);
			
			// create target phrase obj
			TargetPhrase *targetPhrase = new TargetPhrase(Output);
			targetPhrase->CreateFromStringNewFormat(Output, output, targetPhraseString, factorDelimiter);
			
			// alignment
			list<pair<size_t,size_t> > alignmentInfo;
			CreateAlignmentInfo(alignmentInfo, alignString, sourcePhrase, *targetPhrase);

			// rest of target phrase
			targetPhrase->CreateAlignmentInfo(alignmentInfo);
			targetPhrase->SetSourceLHS(sourceLHS);
			targetPhrase->SetTargetLHS(targetLHS);
			//targetPhrase->SetDebugOutput(string("New Format pt ") + line);
			
			// component score, for n-best output
			std::transform(scoreVector.begin(),scoreVector.end(),scoreVector.begin(),NegateScore);
			std::transform(scoreVector.begin(),scoreVector.end(),scoreVector.begin(),FloorScore);
			
			if (GetPhraseTableImplementation() == GlueRule)
			{
				targetPhrase->SetScoreChart(this, scoreVector, weight, languageModels, false);
			}
			else
			{
				targetPhrase->SetScoreChart(this, scoreVector, weight, languageModels, true);
			}
			
			TargetPhraseCollection &phraseColl = GetOrCreateTargetPhraseCollection(sourcePhrase, *targetPhrase, alignmentInfo);
			AddEquivPhrase(phraseColl, targetPhrase);
			
			count++;
		}
		
		// cleanup cache
		
		// sort each target phrase collection
		m_collection.Sort(m_tableLimit);
		
		return true;
	}
		
	TargetPhraseCollection &PhraseDictionaryNewFormat::GetOrCreateTargetPhraseCollection(const Phrase &source, const TargetPhrase &target
																																											 , list<pair<size_t,size_t> > &alignmentInfo)
	{
		PhraseDictionaryNodeSourceLabel &currNode = GetOrCreateNode(source, target, alignmentInfo);
		return currNode.GetOrCreateTargetPhraseCollection();
	}
	
	PhraseDictionaryNodeSourceLabel &PhraseDictionaryNewFormat::GetOrCreateNode(const Phrase &source, const TargetPhrase &target
																																							, list<pair<size_t,size_t> > &alignmentInfo)
	{
		const size_t size = source.GetSize();
		
		// sort alignment info into source order
		map<size_t, size_t> sortedAlignment;
		list<pair<size_t,size_t> >::const_iterator iterAlign;
		for (iterAlign = alignmentInfo.begin(); iterAlign != alignmentInfo.end(); ++iterAlign)
		{
			sortedAlignment[iterAlign->first] = iterAlign->second;
		}
				 		
		PhraseDictionaryNodeSourceLabel *currNode = &m_collection;
		for (size_t pos = 0 ; pos < size ; ++pos)
		{
			const Word& word = source.GetWord(pos);

			if (word.IsNonTerminal())
			{ // indexed by source label 1st
				const Word &sourceNonTerm = word;
				
				size_t targetNonTermInd = sortedAlignment[pos];
				const Word &targetNonTerm = target.GetWord(targetNonTermInd);
				currNode = currNode->GetOrCreateChild(targetNonTerm, sourceNonTerm);

			}
			else
			{
				currNode = currNode->GetOrCreateChild(word, word);
			}
			
			assert(currNode != NULL);
		}
		
		return *currNode;
	}
	
	void PhraseDictionaryNewFormat::AddEquivPhrase(TargetPhraseCollection	&targetPhraseColl, TargetPhrase *targetPhrase)
	{
		targetPhraseColl.Add(targetPhrase);
	}
	
	
	const TargetPhraseCollection *PhraseDictionaryNewFormat::GetTargetPhraseCollection(const Phrase &source) const
	{ // exactly like CreateTargetPhraseCollection, but don't create
		assert(false);
		return NULL;
		/*
		 const size_t size = source.GetSize();
		 
		 const PhraseDictionaryNodeSourceLabel *currNode = &m_collection;
		 for (size_t pos = 0 ; pos < size ; ++pos)
		 {
		 const Word& word = source.GetWord(pos);
		 currNode = currNode->GetChild(word);
		 if (currNode == NULL)
		 return NULL;
		 }
		 
		 return currNode->GetTargetPhraseCollection();
		 */
	}
	
	void PhraseDictionaryNewFormat::InitializeForInput(const InputType& input)
	{
		assert(m_runningNodesVec.size() == 0);
		size_t sourceSize = input.GetSize();
		m_runningNodesVec.resize(sourceSize);
		
		for (size_t ind = 0; ind < m_runningNodesVec.size(); ++ind)
		{
			ProcessedRule *initProcessedRule = new ProcessedRule(m_collection);
			
			ProcessedRuleStack *processedStack = new ProcessedRuleStack(sourceSize - ind + 1);
			processedStack->Add(0, initProcessedRule); // init rule. stores the top node in tree
			
			m_runningNodesVec[ind] = processedStack;
		}
	}
	
	PhraseDictionaryNewFormat::~PhraseDictionaryNewFormat()
	{
		CleanUp();
	}
	
	void PhraseDictionaryNewFormat::SetWeightTransModel(const vector<float> &weightT)
	{
		PhraseDictionaryNodeSourceLabel::iterator iterDict;
		for (iterDict = m_collection.begin() ; iterDict != m_collection.end() ; ++iterDict)
		{
			PhraseDictionaryNodeSourceLabel::InnerNodeMap &innerNode = iterDict->second;
			PhraseDictionaryNodeSourceLabel::InnerNodeMap::iterator iterInner;
			for (iterInner = innerNode.begin() ; iterInner != innerNode.end() ; ++iterInner)
			{
				// recursively set weights in nodes
				PhraseDictionaryNodeSourceLabel &node = iterInner->second;
				node.SetWeightTransModel(this, weightT);
			}
		}
	}
	
	
	void PhraseDictionaryNewFormat::CleanUp()
	{
		//RemoveAllInColl(m_chartTargetPhraseColl);
		std::vector<ChartRuleCollection*>::iterator iter;
		for (iter = m_chartTargetPhraseColl.begin(); iter != m_chartTargetPhraseColl.end(); ++iter)
		{
			ChartRuleCollection *item = *iter;
			ChartRuleCollection::Delete(item);
		}
		m_chartTargetPhraseColl.clear();
		
		RemoveAllInColl(m_runningNodesVec);
	}
	
	TO_STRING_BODY(PhraseDictionaryNewFormat);
	
	// friend
	ostream& operator<<(ostream& out, const PhraseDictionaryNewFormat& phraseDict)
	{
		const PhraseDictionaryNodeSourceLabel &coll = phraseDict.m_collection;
		PhraseDictionaryNodeSourceLabel::const_iterator iter;
		for (iter = coll.begin() ; iter != coll.end() ; ++iter)
		{
			const Word &word = (*iter).first;
			out << word;
		}
		return out;
	}
	
	
}

