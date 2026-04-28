// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Marc Sturm $
// --------------------------------------------------------------------------

#pragma once

// OpenMS_GUI config
#include <OpenMS/VISUAL/OpenMS_GUIConfig.h>

#include <OpenMS/DATASTRUCTURES/Param.h>
#include <OpenMS/VISUAL/LayerDataBase.h>

#include <tuple>
#include <utility>
#include <vector>

class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QString;
class QWidget;

#include <QtWidgets/QDialog>

namespace OpenMS
{
  class ParamEditor;
  class PlotCanvas;
  class TVToolDiscovery;

  /**
  @brief TOPP tool selection dialog

  In the dialog, the user can
    - select a TOPP tool
    - select the options used for the input and output file
    - and set the parameters for the tool

  This information can then be used to execute the tool.

  The offered tools depend on the data type set in the constructor.

  @ingroup Dialogs
  */
  class OPENMS_GUI_DLLAPI ToolsDialog :
    public QDialog
  {
    Q_OBJECT

public:
    /**
      @brief Constructor

      @param[in] parent Qt parent widget
      @param[in] params Containing all TOPP tool/util params
      @param[in] ini_file The file name of the temporary INI file created by this dialog
      @param[in] default_dir The default directory for loading and storing INI files
      @param[in] layer_type The type of data (determines the applicable tools)
      @param[in] layer_name The name of the selected layer
      @param[in] tool_scanner Pointer to the tool scanner for access to the plugins and to rerun the plugins detection
    */
    ToolsDialog(QWidget * parent, const Param& params, String ini_file, String default_dir, LayerDataBase::DataType layer_type, const String& layer_name, PlotCanvas* canvas, Size active_layer_index, TVToolDiscovery* tool_scanner);
    ///Destructor
    ~ToolsDialog() override;

    /// to get the parameter name for output. Empty if no output was selected.
    String getOutput();
    /// to get the parameter name for input
    String getInput();
    /// to get the currently selected tool-name
    String getTool();
    /// get the default extension for the output file
    String getExtension();

    /// Returns selected input mappings as (tool parameter name, layer index)
    std::vector<std::pair<String, Size>> getInputLayerBindings() const;

    /// Returns selected output mappings as (tool parameter name, keep as new layer, extension, required)
    std::vector<std::tuple<String, bool, String, bool>> getOutputBindings() const;


private:
    /// ParamEditor for reading ini-files
    ParamEditor * editor_;
    /// Label for CPU usage row
    QLabel * cpu_usage_label_;
    /// Container for thread controls (FastMode + manual controls)
    QWidget * threads_widget_;
    /// Enables automatic use of all available threads
    QCheckBox * fast_mode_checkbox_;
    /// Manual thread count dropdown (1..max)
    QComboBox * threads_combo_;
    /// Maximum available thread count from OpenMP
    int max_threads_;
    /// Whether the current tool offers a threads parameter
    bool has_threads_param_;
    /// tools description label
    QLabel * tool_desc_;
    /// ComboBox for choosing a TOPP-tool
    QComboBox * tools_combo_;
    /// Button to rerun the automatic plugin detection
    QPushButton * reload_plugins_button_;
    /// input mapping container widget
    QWidget * input_mapping_widget_;
    /// output mapping container widget
    QWidget * output_mapping_widget_;

    struct InputMappingRow
    {
      String param_name;
      bool required = false;
      std::vector<String> extensions;
      std::vector<Size> eligible_layers;
      QComboBox* layer_combo = nullptr;
      QLabel* param_label = nullptr;
      QLabel* ext_label = nullptr;
      Size selected_layer = Size(-1);
    };

    struct OutputMappingRow
    {
      String param_name;
      bool required = false;
      std::vector<String> extensions;
      QComboBox* action_combo = nullptr;
      QLabel* param_label = nullptr;
      QLabel* ext_label = nullptr;
      bool keep_as_new_layer = true;
    };

    std::vector<InputMappingRow> input_rows_;
    std::vector<OutputMappingRow> output_rows_;
    /// Param for loading the ini-file
    Param arg_param_;
    /// Param for loading configuration information in the ParamEditor
    Param vis_param_;
    /// Param containing only parameters shown/edited in the ParamEditor (GUI subset)
    Param editor_param_;
    /// ok-button connected with slot ok_()
    QPushButton * ok_button_;
    /// Location of the temporary INI file this dialog works on
    String ini_file_;
    /// default-dir of ini-file to open
    String default_dir_;
    /// name of ini-file
    QString filename_;
    /// Mapping of file extension to layer type to determine the type of a tool
    std::map<String, LayerDataBase::DataType> tool_map_;
    /// Param object containing all TOPP tool/util params
    Param tool_params_;
    /// Param object containing all plugin params
    Param plugin_params_;
    /// Pointer to the tool scanner for access to the plugins and to rerun the plugins detection
    TVToolDiscovery * tool_scanner_;
    /// The layer type of the current layer to determine all usable plugins
    LayerDataBase::DataType layer_type_;
    /// Plot canvas for layer selection in input mappings
    PlotCanvas* canvas_;
    /// currently active layer index (used for initial selection)
    Size active_layer_index_;

    /// Disables the ok button and mapping controls
    void disable_();
    /// Enables the ok button and mapping controls
    void enable_();
    /// Determine all types a tool is compatible with by mapping each file extensions in a tools param
    std::vector<LayerDataBase::DataType> getTypesFromParam_(const Param& p) const;
    /// Build input/output mapping rows and widgets from the specified parameter object.
    void setInputOutputCombo_(const Param& p);
    /// Rebuild available layer choices for all input rows while enforcing unique layer usage.
    void refreshInputLayerCombos_();
    /// Build available input layer list for a given extension filter.
    std::vector<Size> findCompatibleLayers_(const std::vector<String>& extensions) const;
    /// Convert layer data type to default file extension used by TOPPView export.
    static String layerTypeToDefaultExtension_(LayerDataBase::DataType type);
    /// Create a list of all TOPP tool/util/plugins that are compatible with the active layer type
    QStringList createToolsList_();
    /// Populate and initialize thread controls
    void initializeThreadsControls_();
    /// Show or hide thread controls based on current tool support
    void updateThreadsControlsVisibility_();
    /// Synchronize manual controls and fast mode based on current vis_param_ value
    void syncThreadsControlsFromVisParam_(bool default_fast_mode);
    /// Apply the selected thread mode/value back to vis_param_
    bool applyThreadsToVisParam_();
    /// Clamp requested thread count to valid range [1, max_threads_]
    int clampThreadCount_(int value) const;
    /// Build GUI-only editor parameters from internal parameter state
    void updateEditorParamFromVisParam_();
    /// Merge edited GUI-only parameters back into internal parameter state
    void mergeEditorParamIntoVisParam_();

protected slots:

    /// if ok button pressed show the tool output in a new layer, a new window or standard output as messagebox
    void ok_();
    /// Slot that handles changing of the tool
    void setTool_(int i);
    /// Slot that retrieves and displays the defaults
    void createINI_();
    /// loads an ini-file into the editor
    void loadINI_();
    /// stores an ini-file from the editor
    void storeINI_();
    /// rerun the automatic plugin detection
    void reloadPlugins_();
    /// Slot toggling between fast and manual thread mode
    void fastModeToggled_(bool checked);
    /// Slot handling predefined manual thread selection from combo
    void manualThreadsComboChanged_(int index);
  };

}