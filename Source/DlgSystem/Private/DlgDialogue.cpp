// Copyright 2017-2018 Csaba Molnar, Daniel Butum
#include "DlgDialogue.h"

#include "DevObjectVersion.h"
#include "FileManager.h"
#include "Paths.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#endif

#include "DlgSystemPrivatePCH.h"
#include "IO/DlgConfigParser.h"
#include "IO/DlgConfigWriter.h"
#include "IO/DlgJsonWriter.h"
#include "IO/DlgJsonParser.h"
#include "DlgNode_Speech.h"
#include "DlgNode_End.h"
#include "DlgManager.h"

// Unique DlgDialogue Object version id, generated with random
const FGuid FDlgDialogueObjectVersion::GUID(0x2B8E5105, 0x6F66348F, 0x2A8A0B25, 0x9047A071);
// Register Dialogue custom version with Core
FDevVersionRegistration GRegisterDlgDialogueObjectVersion(FDlgDialogueObjectVersion::GUID,
														  FDlgDialogueObjectVersion::LatestVersion, TEXT("Dev-DlgDialogue"));

// Update dialogue up to the ConvertedNodesToUObject version
void UpdateDialogueToVersion_ConvertedNodesToUObject(UDlgDialogue* Dialogue)
{
	// No Longer supported, get data from text file, and reconstruct everything
	Dialogue->InitialSyncWithTextFile();
#if WITH_EDITOR
	// Force clear the old graph
	Dialogue->ClearGraph();
#endif
}

// Update dialogue up to the UseOnlyOneOutputAndInputPin version
void UpdateDialogueToVersion_UseOnlyOneOutputAndInputPin(UDlgDialogue* Dialogue)
{
#if WITH_EDITOR
	Dialogue->GetDialogueEditorModule()->UpdateDialogueToVersion_UseOnlyOneOutputAndInputPin(Dialogue);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin UObject interface
void UDlgDialogue::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	DlgName = GetDlgFName();
	OnAssetSaved();
}

void UDlgDialogue::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDlgDialogueObjectVersion::GUID);
	Super::Serialize(Ar);
	const int32 DialogueVersion = Ar.CustomVer(FDlgDialogueObjectVersion::GUID);
	if (DialogueVersion < FDlgDialogueObjectVersion::ConvertedNodesToUObject)
	{
		// No Longer supported
		return;
	}
}

void UDlgDialogue::PostLoad()
{
	Super::PostLoad();
	const int32 DialogueVersion = GetLinkerCustomVersion(FDlgDialogueObjectVersion::GUID);
	// Old files, UDlgNode used to be a FDlgNode
	if (DialogueVersion < FDlgDialogueObjectVersion::ConvertedNodesToUObject)
	{
		UpdateDialogueToVersion_ConvertedNodesToUObject(this);
	}

	// Simplified and reduced the number of pins (only one input/output pin), used for the new visualization
	if (DialogueVersion < FDlgDialogueObjectVersion::UseOnlyOneOutputAndInputPin)
	{
		UpdateDialogueToVersion_UseOnlyOneOutputAndInputPin(this);
	}

	// Simply the number of nodes, VirtualParent Node is merged into Speech Node and SelectRandom and SelectorFirst are merged into one Selector Node
	if (DialogueVersion < FDlgDialogueObjectVersion::MergeVirtualParentAndSelectorTypes)
	{
		UE_LOG(LogDlgSystem,
			   Warning,
			   TEXT("Dialogue = `%s` with Version MergeVirtualParentAndSelectorTypes will not be converted."
				    "See https://gitlab.com/snippets/1691704 for manual conversion "), *GetTextFilePathName());
	}

	// Refresh the data, so that it is valid after loading.
	if (DialogueVersion < FDlgDialogueObjectVersion::ConvertDialogueDataArraysToSets)
	{
		RefreshData();
	}

	// Create thew new Guid
	if (!DlgGuid.IsValid())
	{
		DlgGuid = FGuid::NewGuid();
		UE_LOG(LogDlgSystem, Verbose, TEXT("Creating new DlgGuid = `%s` for Dialogue = `%s` because of of invalid DlgGuid."),
			   *DlgGuid.ToString(), *GetPathName());
	}

#if WITH_EDITOR
	const bool bHasDialogueEditorModule = GetDialogueEditorModule().IsValid();
	// If this is false it means the graph nodes are not even created? Check for old files that were saved
	// before graph editor was even implemented. The editor will popup a prompt from FDialogueEditorUtilities::TryToCreateDefaultGraph
	if (bHasDialogueEditorModule && !GetDialogueEditorModule()->AreDialogueNodesInSyncWithGraphNodes(this))
	{
		return;
	}
#endif

	// Check Nodes for validity
	const int32 NodesNum = Nodes.Num();
	for (int32 NodeIndex = 0; NodeIndex < NodesNum; NodeIndex++)
	{
		UDlgNode* Node = Nodes[NodeIndex];
#if WITH_EDITOR
		if (bHasDialogueEditorModule)
		{
			checkf(Node->GetGraphNode(), TEXT("Expected DialogueVersion = %d to have a valid GraphNode for Node index = %d :("), DialogueVersion, NodeIndex);
		}
#endif
		// Check children point to the right Node
		const TArray<FDlgEdge>& NodeEdges = Node->GetNodeChildren();
		const int32 EdgesNum = NodeEdges.Num();
		for (int32 EdgeIndex = 0; EdgeIndex < EdgesNum; EdgeIndex++)
		{
			const FDlgEdge& Edge = NodeEdges[EdgeIndex];
			if (!Edge.IsValid())
			{
				continue;
			}

			if (!Nodes.IsValidIndex(Edge.TargetIndex))
			{
				UE_LOG(LogDlgSystem, Fatal,
					TEXT("Node with index = %d does not have a valid Edge index = %d with TargetIndex = %d"), NodeIndex, EdgeIndex, Edge.TargetIndex);
			}
		}
	}
}

void UDlgDialogue::PostInitProperties()
{
	// TODO, this seems like a bad place to init properties, because this will get called every time we are loading uassets from the filesystem
	Super::PostInitProperties();
	const int32 DialogueVersion = GetLinkerCustomVersion(FDlgDialogueObjectVersion::GUID);

#if WITH_EDITOR
	// Wait for the editor module to be set by the editor in UDialogueGraph constructor
	if (GetDialogueEditorModule().IsValid())
	{
		CreateGraph();
	}
#endif // #if WITH_EDITOR

	// Keep Name in sync with the file name
	DlgName = GetDlgFName();

	// Used when creating new Dialogues
	// Initialize with a valid GUID
	if (DialogueVersion >= FDlgDialogueObjectVersion::AddGuid && !DlgGuid.IsValid())
	{
		DlgGuid = FGuid::NewGuid();
		UE_LOG(LogDlgSystem, Verbose, TEXT("Creating new DlgGuid = `%s` for Dialogue = `%s` because of new created Dialogue."),
			   *DlgGuid.ToString(), *GetPathName());
	}
}

void UDlgDialogue::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
	DlgName = GetDlgFName();
}

bool UDlgDialogue::Modify(bool bAlwaysMarkDirty)
{
	if (!CanModify())
	{
		return false;
	}

	bool bWasSaved = Super::Modify(bAlwaysMarkDirty);
	// if (StartNode)
	// {
	// 	bWasSaved = bWasSaved && StartNode->Modify(bAlwaysMarkDirty);
	// }

	// for (UDlgNode* Node : Nodes)
	// {
	// 	bWasSaved = bWasSaved && Node->Modify(bAlwaysMarkDirty);
	// }

	return bWasSaved;
}

void UDlgDialogue::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// Used when duplicating dialogues.
	// Make new guid for this copied Dialogue.
	DlgGuid = FGuid::NewGuid();
	UE_LOG(LogDlgSystem, Verbose, TEXT("Creating new DlgGuid = `%s` for Dialogue = `%s` because Dialogue was copied."),
		   *DlgGuid.ToString(), *GetPathName());
}

void UDlgDialogue::PostEditImport()
{
	Super::PostEditImport();

	// Used when duplicating dialogues.
	// Make new guid for this copied Dialogue
	DlgGuid = FGuid::NewGuid();
	UE_LOG(LogDlgSystem, Verbose, TEXT("Creating new DlgGuid = `%s` for Dialogue = `%s` because Dialogue was copied."),
		   *DlgGuid.ToString(), *GetPathName());
}

#if WITH_EDITOR
TSharedPtr<IDlgDialogueEditorModule> UDlgDialogue::DialogueEditorModule = nullptr;

bool UDlgDialogue::CanEditChange(const UProperty* InProperty) const
{
	// Graph view changed, but data was not updated from the graph, prevent stupid mistakes
//	if (GetOutermost()->IsDirty())
//		return false;

	return Super::CanEditChange(InProperty);
}

void UDlgDialogue::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshData();

	// Signal to the listeners
	check(OnDialoguePropertyChanged.IsBound());
	OnDialoguePropertyChanged.Broadcast(PropertyChangedEvent);
}

void UDlgDialogue::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	// Add the graph to the list of referenced objects
	UDlgDialogue* This = CastChecked<UDlgDialogue>(InThis);
	Collector.AddReferencedObject(This->DlgGraph, This);
	Super::AddReferencedObjects(InThis, Collector);
}
// End UObject interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin own functions
void UDlgDialogue::CreateGraph()
{
	// The Graph will only be null if this is the first time we are creating the graph for the Dialogue.
	// After the Dialogue asset is saved, the Dialogue will get the dialogue from the serialized uasset.
	if (DlgGraph != nullptr)
	{
		return;
	}

	if (StartNode == nullptr)
	{
		StartNode = ConstructDialogueNode<UDlgNode_Speech>();
	}

	UE_LOG(LogDlgSystem, Verbose, TEXT("Creating graph for Dialogue = `%s`"), *GetPathName());
	DlgGraph = GetDialogueEditorModule()->CreateNewDialogueGraph(this);

	// Give the schema a chance to fill out any required nodes
	DlgGraph->GetSchema()->CreateDefaultNodesForGraph(*DlgGraph);
	MarkPackageDirty();
}

void UDlgDialogue::ClearGraph()
{
	if (DlgGraph == nullptr)
	{
		return;
	}

	UE_LOG(LogDlgSystem, Verbose, TEXT("Clearing graph for Dialogue = `%s`"), *GetPathName());
	GetDialogueEditorModule()->RemoveAllGraphNodes(this);

	// Give the schema a chance to fill out any required nodes
	DlgGraph->GetSchema()->CreateDefaultNodesForGraph(*DlgGraph);
	MarkPackageDirty();
}

void UDlgDialogue::CompileDialogueNodesFromGraphNodes()
{
	if (!bCompileDialogue)
	{
		return;
	}

	UE_LOG(LogDlgSystem, Log, TEXT("Compiling Dialogue = `%s` (Graph data -> Dialogue data)`"), *GetPathName());
	GetDialogueEditorModule()->CompileDialogueNodesFromGraphNodes(this);
}
#endif // #if WITH_EDITOR

void UDlgDialogue::ReloadFromFile()
{
	// Simply ignore reloading
	const EDlgDialogueTextFormat TextFormat = GetDefault<UDlgSystemSettings>()->DialogueTextFormat;
	if (TextFormat == EDlgDialogueTextFormat::DlgDialogueNoTextFormat)
	{
		RefreshData();
		return;
	}

	StartNode = nullptr;
	Nodes.Empty();

	// TODO handle DlgName == NAME_None or invalid filename
	const FString& TextFileName = GetTextFilePathName();
	UE_LOG(LogDlgSystem, Log, TEXT("Reloading data for Dialogue = `%s` FROM file = `%s`"), *GetPathName(), *TextFileName);

	// TODO(leyyin): Check for errors
	check(TextFormat != EDlgDialogueTextFormat::DlgDialogueNoTextFormat);
	switch (TextFormat)
	{
		case EDlgDialogueTextFormat::DlgDialogueTextFormatJson:
		{
			DlgJsonParser JsonParser;
			JsonParser.InitializeParser(TextFileName);
			JsonParser.ReadAllProperty(GetClass(), this, this);
			break;
		}
		case EDlgDialogueTextFormat::DlgDialogueTextFormatDialogue:
		default:
		{
			DlgConfigParser Parser;
			Parser.InitializeParser(TextFileName);
			Parser.ReadAllProperty(GetClass(), this, this);
			break;
		}
	}

	if (StartNode == nullptr)
	{
		StartNode = ConstructDialogueNode<UDlgNode_Speech>();
	}

	// TODO(leyyin): validate if data is legit, indicies exist and that sort.
	// Check if Guid is not a duplicate
	TArray<UDlgDialogue*> DuplicateDialogues = UDlgManager::GetDialoguesWithDuplicateGuid();
	if (DuplicateDialogues.Num() > 0)
	{
		if (DuplicateDialogues.Contains(this))
		{
			// found duplicate of this Dialogue
			DlgGuid = FGuid::NewGuid();
			UE_LOG(LogDlgSystem,
				Warning,
				TEXT("Creating new DlgGuid = `%s` for Dialogue = `%s` because the input file contained a duplicate GUID."),
				*DlgGuid.ToString(), *GetPathName());
		}
		else
		{
			// We have bigger problems on our hands
			UE_LOG(LogDlgSystem,
				Error,
				TEXT("Found Duplicate Dialogue that does not belong to this Dialogue = `%s`, DuplicateDialogues.Num = %d"),
				*GetPathName(),  DuplicateDialogues.Num());
		}
	}

	DlgName = GetDlgFName();
	AutoFixGraph();
	RefreshData();
}

void UDlgDialogue::OnAssetSaved()
{
#if WITH_EDITOR
	// Compile, graph data -> dialogue data
	CompileDialogueNodesFromGraphNodes();
#endif

	// Save file, dialogue data -> text file (.dlg)
	RefreshData();
	ExportToFile();
}

void UDlgDialogue::ExportToFile() const
{
	const EDlgDialogueTextFormat TextFormat = GetDefault<UDlgSystemSettings>()->DialogueTextFormat;
	if (TextFormat == EDlgDialogueTextFormat::DlgDialogueNoTextFormat)
	{
		// Simply ignore saving
		return;
	}

	// TODO(leyyin): Check for errors
	const FString& TextFileName = GetTextFilePathName();
	UE_LOG(LogDlgSystem, Log, TEXT("Exporting data for Dialogue = `%s` TO file = `%s`"), *GetPathName(), *TextFileName);

	check(TextFormat != EDlgDialogueTextFormat::DlgDialogueNoTextFormat)
	switch (TextFormat)
	{
		case EDlgDialogueTextFormat::DlgDialogueTextFormatJson:
		{
			DlgJsonWriter JsonWriter(GetClass(), this);
			JsonWriter.ExportToFile(TextFileName);
			break;
		}
		case EDlgDialogueTextFormat::DlgDialogueTextFormatDialogue:
		default:
		{
			DlgConfigWriter DlgWriter(GetClass(), this);
			DlgWriter.ExportToFile(TextFileName);
			break;
		}
	}
}

void UDlgDialogue::RefreshData()
{
	UE_LOG(LogDlgSystem, Log, TEXT("Refreshing data for Dialogue = `%s`"), *GetPathName());
	DlgData.Empty();

	for (UDlgNode* Node : Nodes)
	{
		TArray<FName> Participants;
		Node->GetAssociatedParticipants(Participants);
		for (const FName& Participant : Participants)
		{
			if (!DlgData.Contains(Participant))
			{
				DlgData.Add(Participant);
			}
		}

		// TODO warn user about duplicate values in the FDlgParticipantData
		// gets map entry - creates it first if it is not yet there
		auto GetParticipantDataEntry = [this](const FName& ParticipantName,
											  const FName& FallbackNodeOwnerName) -> FDlgParticipantData&
		{
			// If the Participant Name is not set, it adopts the Node Owner Name
			const FName& Name = ParticipantName == NAME_None ? FallbackNodeOwnerName : ParticipantName;

			FDlgParticipantData* ParticipantData = DlgData.Find(Name);
			if (ParticipantData == nullptr)
			{
				ParticipantData = &DlgData.Add(Name);
			}

			return *ParticipantData;
		};

		auto AddCondition = [this, &GetParticipantDataEntry](const FName& ParticipantName,
															 const FName& FallbackNodeOwnerName,
															 const EDlgConditionType& ConditionType,
															 const FName& ConditionName)
		{
			FDlgParticipantData& ParticipantData = GetParticipantDataEntry(ParticipantName, FallbackNodeOwnerName);
			switch (ConditionType)
			{
				case EDlgConditionType::DlgConditionIntCall:
					ParticipantData.IntVariableNames.Add(ConditionName);
					break;
				case EDlgConditionType::DlgConditionFloatCall:
					ParticipantData.FloatVariableNames.Add(ConditionName);
					break;
				case EDlgConditionType::DlgConditionBoolCall:
					ParticipantData.BoolVariableNames.Add(ConditionName);
					break;
				case EDlgConditionType::DlgConditionNameCall:
					ParticipantData.NameVariableNames.Add(ConditionName);
					break;
				case EDlgConditionType::DlgConditionEventCall:
					ParticipantData.Conditions.Add(ConditionName);
					break;
				default:
					break;
			}
		};

		// 1.1: Conditions from nodes
		for (const FDlgCondition& Condition : Node->GetNodeEnterConditions())
		{
			AddCondition(Condition.ParticipantName, Node->GetNodeParticipantName(), Condition.ConditionType, Condition.CallbackName);
		}
		// 1.2: Conditions from edges
		for (const FDlgEdge& Edge : Node->GetNodeChildren())
		{
			for (const FDlgCondition& Condition : Edge.Conditions)
			{
				AddCondition(Condition.ParticipantName, Node->GetNodeParticipantName(), Condition.ConditionType, Condition.CallbackName);
			}
		}

		// 2: Events
		for (const FDlgEvent& Event : Node->GetNodeEnterEvents())
		{
			FDlgParticipantData& ParticipantData = GetParticipantDataEntry(Event.ParticipantName, Node->GetNodeParticipantName());

			switch (Event.EventType)
			{
				case EDlgEventType::DlgEventEvent:
					ParticipantData.Events.Add(Event.EventName);
					break;

				case EDlgEventType::DlgEventModifyInt:
					ParticipantData.IntVariableNames.Add(Event.EventName);
					break;

				case EDlgEventType::DlgEventModifyFloat:
					ParticipantData.FloatVariableNames.Add(Event.EventName);
					break;

				case EDlgEventType::DlgEventModifyBool:
					ParticipantData.BoolVariableNames.Add(Event.EventName);
					break;

				case EDlgEventType::DlgEventModifyName:
					ParticipantData.NameVariableNames.Add(Event.EventName);
					break;

				default:
					break;
			}
		}
	}
}

void UDlgDialogue::AutoFixGraph()
{
	check(StartNode);
	// syntax correction 1: if there is no start node, we create one pointing to the first node
	if (StartNode->GetNodeChildren().Num() == 0 && Nodes.Num() > 0)
	{
		StartNode->AddNodeChild({ 0 });
	}
	StartNode->SetFlags(RF_Transactional);

	// syntax correction 2: if there is no end node, we add one
	bool bHasEndNode = false;
	// check if the end node is already there
	for (UDlgNode* Node : Nodes)
	{
		check(Node);
		Node->SetFlags(RF_Transactional);
		if (Node->IsA<UDlgNode_End>())
		{
			bHasEndNode = true;
			break;
		}
	}
	// add it if not
	if (!bHasEndNode && Nodes.Num() > 0)
	{
		auto* EndNode = ConstructDialogueNode<UDlgNode_End>();
		EndNode->SetNodeParticipantName(Nodes[0]->GetNodeParticipantName());
		Nodes.Add(EndNode);
	}

	// syntax correction 3: if a node is not an end node but has no children it will "adopt" the next node
	for (int32 i = 0; i < Nodes.Num() - 1; ++i)
	{
		UDlgNode* Node = Nodes[i];
		const TArray<FDlgEdge>& NodeChildren = Node->GetNodeChildren();

		if (!Node->IsA<UDlgNode_End>() && NodeChildren.Num() == 0)
		{
			Node->AddNodeChild({ i + 1 });
		}

		// Add some text to the edges.
		if (NodeChildren.Num() == 1 && Nodes.IsValidIndex(NodeChildren[0].TargetIndex))
		{
			UDlgNode* NextNode = Nodes[NodeChildren[0].TargetIndex];
			if (NextNode->IsA<UDlgNode_End>())
			{
				Node->GetMutableNodeChildAt(0)->Text = FText::FromString("Finish");
			}
			else
			{
				Node->GetMutableNodeChildAt(0)->Text = FText::FromString("Next");
			}
		}
	}
}

FString UDlgDialogue::GetTextFilePathName(bool bAddExtension/* = true*/) const
{
	// Extract filename from path
	// Note: this is not a filesystem path, it is an unreal path 'Outermost.[Outer:]Name'
	// Usually GetPathName works, but the path name might be weird.
	// FSoftObjectPath(this).ToString(); which does call this function GetPathName() but it returns a legit clean path
	// if it is in the wrong format
	FString TextFileName = GetTextFilePathNameFromAssetPathName(FSoftObjectPath(this).ToString());
	if (bAddExtension)
	{
		// Modify the extension of the base text file depending on the extension
		TextFileName += GetTextFileExtension(GetDefault<UDlgSystemSettings>()->DialogueTextFormat);
	}

	return TextFileName;
}

FString UDlgDialogue::GetTextFilePathNameFromAssetPathName(const FString& AssetPathName)
{
	static constexpr const TCHAR* Separator = TEXT("/");

	// Get rid of the extension from `filename.extension` from the end of the path
	FString PathName = FPaths::GetBaseFilename(AssetPathName, false);

	// Get rid of the first folder, Game/ or Name/ (if in the plugins dir) from the beginning of the path.
	// Are we in the game directory?
	FString ContentDir = FPaths::ProjectContentDir();
	if (!PathName.RemoveFromStart(TEXT("/Game/")))
	{
		// We are in the plugins dir
		TArray<FString> PathParts;
		PathName.ParseIntoArray(PathParts, Separator);
		if (PathParts.Num() > 0)
		{
			const FString PluginName = PathParts[0];
			const FString PluginDir = FPaths::ProjectPluginsDir() / PluginName;

			// Plugin exists
			if (FPaths::DirectoryExists(PluginDir))
			{
				ContentDir = PluginDir / TEXT("Content/");
			}

			// remove plugin name
			PathParts.RemoveAt(0);
			PathName = FString::Join(PathParts, Separator);
		}
	}

	return ContentDir + PathName;
}

FString UDlgDialogue::GetTextFileExtension(EDlgDialogueTextFormat InTextFormat)
{
	switch (InTextFormat)
	{
		// Empty
		case EDlgDialogueTextFormat::DlgDialogueNoTextFormat:
			return FString();

		// JSON has the .json added at the end
		case EDlgDialogueTextFormat::DlgDialogueTextFormatJson:
			return TEXT(".dlg.json");

		case EDlgDialogueTextFormat::DlgDialogueTextFormatDialogue:
		default:
			return TEXT(".dlg");
	}
}

// End own functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
