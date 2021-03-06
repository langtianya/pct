#include "vsparsing.h"
#include <boost/filesystem/path.hpp>
#include <stdexcept>
#include <vector>
#include <string>
#include <regex>
#include <fstream>


using namespace tinyxml2;
using namespace boost::filesystem;
using namespace std;

VcxprojParsing::VcxprojParsing(const char* path,
	                           std::ostream& errStream)
	: errorStream(errStream)
{			
	boost::filesystem::path filepath;
	
	if (doc.LoadFile(path) != XMLError::XML_SUCCESS)
		throw runtime_error(string("Cannot open: ") + path + ": " + doc.ErrorName());
	
	filepath = boost::filesystem::path(path);	

	if (filepath.extension().string() == ".vcproj")
		errStream << "Warning: when reading: " << path << ". vcproj files are not supported. Please upgrade your solution to a Visual Studio 2010 solution or later";
}

void VcxprojParsing::replaceEnvVars(string& paths)
{	
	// both %MYVAR% or $(MYVAR) syntaxes would be expanded as environment
	// variables in .vcxproj 
	regex rgxDollar("\\$\\((.+?)\\)");    
	regex rgxPercentage("%(.+?)%");    
	smatch match;	
	string remaining;
	
	remaining = paths;
	paths.clear();

	while (regex_search(remaining,
		                match,
		                rgxDollar)) {
		const char* envvarvalue = getenv(match[1].str().c_str());
		string completeMatch = match[0].str();

		paths += match.prefix().str() + 
			     (envvarvalue ? envvarvalue : completeMatch.c_str());
		remaining = match.suffix().str();

		if (!envvarvalue && completeMatch != "$(Configuration)")
			errorStream << "Error: Variable not set: " << completeMatch << "\n";
	}

	if (!remaining.empty())
		paths += remaining;

	remaining = paths;
	paths.clear();

	while (regex_search(remaining,
		                match,
		                rgxPercentage)) {
		const char* envvarvalue = getenv(match[1].str().c_str());
		string completeMatch = match[0].str();

		paths += match.prefix().str() +
			(envvarvalue ? envvarvalue : completeMatch.c_str());
		remaining = match.suffix().str();

		if (!envvarvalue)
			errorStream << "Environment variable not set: " << completeMatch << "\n";
	}

	if (!remaining.empty())
		paths += remaining;

}

void VcxprojParsing::parse(vector<ProjectConfiguration>& configurations,
	                  vector<string>& files)
{
	XMLElement* project = project = doc.FirstChildElement("Project");

	if (project) {
		XMLElement* itemGroup = project->FirstChildElement("ItemGroup");
		XMLElement* itemDefinitionGroup = project->FirstChildElement("ItemDefinitionGroup");

		while (itemGroup) {
			const char* labelItemGroup;
			XMLElement* clCompile;

			if ((labelItemGroup = itemGroup->Attribute("Label")) && !strcmp(labelItemGroup, "ProjectConfigurations")) {
				XMLElement* projectConfiguration = itemGroup->FirstChildElement("ProjectConfiguration");

				while (projectConfiguration) {
					const char* label = projectConfiguration->Attribute("Include");
					const char* configurationName = projectConfiguration->FirstChildElement("Configuration")->GetText();

					configurations.push_back({ label, configurationName });
					projectConfiguration = projectConfiguration->NextSiblingElement("ProjectConfiguration");
				}
			}

			clCompile = itemGroup->FirstChildElement("ClCompile");

			while (clCompile) {
				files.push_back(clCompile->Attribute("Include"));
				clCompile = clCompile->NextSiblingElement("ClCompile");
			}

			itemGroup = itemGroup->NextSiblingElement("ItemGroup");
		}

		while (itemDefinitionGroup) {
			for (auto& configuration : configurations) {
				const char* label = itemDefinitionGroup->Attribute("Condition");

				if (label == "'$(Configuration)|$(Platform)'=='" + configuration.name + "'") {
					XMLElement* clCompile = itemDefinitionGroup->FirstChildElement("ClCompile");
					XMLElement* definitions = clCompile ? clCompile->FirstChildElement("PreprocessorDefinitions") : NULL;
					XMLElement* includeDirs = clCompile ? clCompile->FirstChildElement("AdditionalIncludeDirectories") : NULL;
					XMLElement* precompiledHeaderFile = clCompile ? clCompile->FirstChildElement("PrecompiledHeaderFile") : NULL;

					if (definitions && definitions->FirstChild()) {
						configuration.definitions = definitions->FirstChild()->ToText()->Value();
						replaceEnvVars(configuration.definitions);

						// replace things like %(PreprocessorDefinitions) which extract headers cannot understand
						configuration.definitions = regex_replace(configuration.definitions, regex("%\\(.*\\)"), string(""));						
					}

					if (includeDirs && includeDirs->FirstChild()) {
						configuration.additionalIncludeDirectories = includeDirs->FirstChild()->ToText()->Value();
						replaceEnvVars(configuration.additionalIncludeDirectories);
					}

					if (precompiledHeaderFile && precompiledHeaderFile->FirstChild()) {
						configuration.precompiledHeaderFile = precompiledHeaderFile->FirstChild()->ToText()->Value();
						replaceEnvVars(configuration.precompiledHeaderFile);
					}

				}
				itemDefinitionGroup = itemDefinitionGroup->NextSiblingElement("ItemDefinitionGroup");
			}
		}
	}
}

SlnParsing::SlnParsing(const char* path, std::ostream& errStream)
	: errorStream(errStream)
{
	ifstream file(path);
	string line;

	while (getline(file, line)) {
		if (!line.empty())
			fileContents.push_back(line);
	}
}

void SlnParsing::parse(std::vector<Project>& projects)
{	
		for (auto& line : fileContents) {
			regex rgx(R"%(Project\("\{.?.?.?.?.?.?.?.?-.?.?.?.?-.?.?.?.?-.?.?.?.?-.?.?.?.?.?.?.?.?.?.?.?.?\}"\)\s*=\s*"(.*)"\s*,\s*"(.*)"\s*,\s*"\{.?.?.?.?.?.?.?.?-.?.?.?.?-.?.?.?.?-.?.?.?.?-.?.?.?.?.?.?.?.?.?.?.?.?\}\s*")%");
			smatch match;

			regex_search(line, match, rgx);

			if (!match.empty() && match.length() >= 2) {
				Project project;

				project.name = match[1];
				project.location = match[2];
				projects.push_back(project);
			}
		}
}



