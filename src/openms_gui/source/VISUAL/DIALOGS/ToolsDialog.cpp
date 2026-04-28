// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Marc Sturm $
// --------------------------------------------------------------------------

// OpenMS includes
#include <OpenMS/VISUAL/DIALOGS/ToolsDialog.h>

#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/APPLICATIONS/ToolHandler.h>
#include <OpenMS/DATASTRUCTURES/Param.h>
#include <OpenMS/FORMAT/FileTypes.h>
#include <OpenMS/FORMAT/ParamXMLFile.h>
#include <OpenMS/SYSTEM/File.h>
#include <OpenMS/VISUAL/ParamEditor.h>
#include <OpenMS/VISUAL/PlotCanvas.h>
#include <OpenMS/VISUAL/TVToolDiscovery.h>
#include <OpenMS/VISUAL/MISC/CommonDefs.h>
#include <OpenMS/VISUAL/MISC/Qt5Port.h>

#include <QProcess>
#include <QtCore/QStringList>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <omp.h>
#include <utility>


using namespace std;

namespace OpenMS
{

  ToolsDialog::ToolsDialog(
          QWidget* parent,
          const Param& params,
          String ini_file,
          String default_dir,
          LayerDataBase::DataType layer_type,
          const String& layer_name,
          PlotCanvas* canvas,
          Size active_layer_index,
          TVToolDiscovery* tool_scanner) :
            QDialog(parent),
            editor_(nullptr),
            cpu_usage_label_(nullptr),
            threads_widget_(nullptr),
            fast_mode_checkbox_(nullptr),
            threads_combo_(nullptr),
            max_threads_(std::max(1, omp_get_max_threads())),
            has_threads_param_(false),
            input_mapping_widget_(nullptr),
            output_mapping_widget_(nullptr),
            ini_file_(std::move(ini_file)),
            default_dir_(std::move(default_dir)),
            tool_params_(params.copy("tool_params:", true)),
            plugin_params_(),
            tool_scanner_(tool_scanner),
            layer_type_(layer_type),
            canvas_(canvas),
            active_layer_index_(active_layer_index)
  {
    auto main_grid = new QGridLayout(this);

    // Layer label
    auto layer_label = new QLabel("Selected Layer:");
    main_grid->addWidget(layer_label, 0, 0);
    auto layer_label_name = new QLabel(toQString(layer_name));
    main_grid->addWidget(layer_label_name, 0, 1);

    auto label = new QLabel("TOPP tool:");
    main_grid->addWidget(label, 1, 0);

    // Determine all available tools compatible with the layer_type
    tool_map_ = {
            {FileTypes::Type::MZML, LayerDataBase::DataType::DT_PEAK},
            {FileTypes::Type::MZXML, LayerDataBase::DataType::DT_PEAK},
            {FileTypes::Type::FEATUREXML, LayerDataBase::DataType::DT_FEATURE},
            {FileTypes::Type::CONSENSUSXML, LayerDataBase::DataType::DT_CONSENSUS},
            {FileTypes::Type::IDXML, LayerDataBase::DataType::DT_IDENT}
    };

    QStringList list = createToolsList_();

    tools_combo_ = new QComboBox;
    tools_combo_->setMinimumWidth(150);
    tools_combo_->addItems(list);
    connect(tools_combo_, CONNECTCAST(QComboBox, activated, (int)), this, &ToolsDialog::setTool_);

    main_grid->addWidget(tools_combo_, 1, 1);

    reload_plugins_button_ = new QPushButton("Reload Plugins");
    connect(reload_plugins_button_, &QPushButton::clicked, this, &ToolsDialog::reloadPlugins_);
    main_grid->addWidget(reload_plugins_button_, 0, 2);

    label = new QLabel("inputs:");
    main_grid->addWidget(label, 2, 0);
    input_mapping_widget_ = new QWidget(this);
    main_grid->addWidget(input_mapping_widget_, 2, 1, 1, 2);

    label = new QLabel("outputs:");
    main_grid->addWidget(label, 3, 0);
    output_mapping_widget_ = new QWidget(this);
    main_grid->addWidget(output_mapping_widget_, 3, 1, 1, 2);

    // tools description label
    tool_desc_ = new QLabel;
    tool_desc_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    tool_desc_->setWordWrap(true);
    main_grid->addWidget(tool_desc_, 1, 3, 3, 1);

    initializeThreadsControls_();
    cpu_usage_label_ = new QLabel("CPU usage:");
    cpu_usage_label_->setVisible(false);
    main_grid->addWidget(cpu_usage_label_, 4, 0);
    main_grid->addWidget(threads_widget_, 4, 1);

    //Add advanced mode check box
    editor_ = new ParamEditor(this);
    main_grid->addWidget(editor_, 5, 0, 1, 5);

    auto hbox = new QHBoxLayout;
    auto load_button = new QPushButton(tr("&Load"));
    connect(load_button, &QPushButton::clicked, this, &ToolsDialog::loadINI_);
    hbox->addWidget(load_button);
    auto store_button = new QPushButton(tr("&Store"));
    connect(store_button, &QPushButton::clicked, this, &ToolsDialog::storeINI_);
    hbox->addWidget(store_button);
    hbox->addStretch();

    ok_button_ = new QPushButton(tr("&Ok"));
    ok_button_->setAutoDefault(false);
    ok_button_->setDefault(false);
    connect(ok_button_, &QPushButton::clicked, this, &ToolsDialog::ok_);
    hbox->addWidget(ok_button_);

    auto cancel_button = new QPushButton(tr("&Cancel"));
    cancel_button->setAutoDefault(false);
    cancel_button->setDefault(false);
    connect(cancel_button, &QPushButton::clicked, this, &ToolsDialog::reject);
    hbox->addWidget(cancel_button);
    main_grid->addLayout(hbox, 6, 0, 1, 5);

    setLayout(main_grid);

    setWindowTitle(tr("Apply TOPP tool to layer"));
    disable_();
  }

  ToolsDialog::~ToolsDialog() = default;

  std::vector<LayerDataBase::DataType> ToolsDialog::getTypesFromParam_(const Param& p) const
  {
    // Containing all types a tool is compatible with
    std::vector<LayerDataBase::DataType> types;
    for (const auto& entry : p)
    {
      if (entry.name == "in")
      {
        // Map all file extension to a LayerDataBase::DataType
        for (auto& file_extension : entry.valid_strings)
        {
          // a file extension in valid_strings is of form "*.TYPE" -> convert to substr "TYPE".
          const String& file_type = file_extension.substr(2, file_extension.size());
          const auto& iter = tool_map_.find(FileTypes::nameToType(file_type));
          // If mapping was found
          if (iter != tool_map_.end())
          {
            types.push_back(iter->second);
          }
        }
        break;
      }
    }
    return types;
  }

  void ToolsDialog::setInputOutputCombo_(const Param &p)
  {
    auto clear_widget_layout = [](QWidget* widget)
    {
      if (widget == nullptr || widget->layout() == nullptr)
      {
        return;
      }
      QLayout* layout = widget->layout();
      QLayoutItem* item = nullptr;
      while ((item = layout->takeAt(0)) != nullptr)
      {
        if (item->widget() != nullptr)
        {
          delete item->widget();
        }
        delete item;
      }
      delete layout;
    };

    input_rows_.clear();
    output_rows_.clear();

    String str;
    for (Param::ParamIterator iter = p.begin(); iter != p.end(); ++iter)
    {
      // iter.getName() is either of form "ToolName:1:ItemName" or "ToolName:1:NodeName:[...]:ItemName".
      // Cut off "ToolName:1:"
      str = iter.getName().substr(iter.getName().rfind("1:") + 2, iter.getName().size());
      // Only add items and no nodes
      if (!str.empty() && str.find(":") == String::npos)
      {
        // Only add to input list if item has "input file" tag.
        if (iter->tags.find("input file") != iter->tags.end())
        {
          InputMappingRow row;
          row.param_name = str;
          row.required = iter->tags.find("required") != iter->tags.end();
          row.extensions.clear();
          row.extensions.reserve(iter->valid_strings.size());
          for (const auto& ext : iter->valid_strings)
          {
            row.extensions.emplace_back(ext);
          }
          row.eligible_layers = findCompatibleLayers_(row.extensions);
          row.selected_layer = Size(-1);
          if (!row.eligible_layers.empty())
          {
            auto it = std::find(row.eligible_layers.begin(), row.eligible_layers.end(), active_layer_index_);
            row.selected_layer = (it != row.eligible_layers.end()) ? active_layer_index_ : row.eligible_layers.front();
          }
          input_rows_.push_back(std::move(row));
        }
          // Only add to output list if item has "output file" tag.
        else if (iter->tags.find("output file") != iter->tags.end())
        {
          OutputMappingRow row;
          row.param_name = str;
          row.required = iter->tags.find("required") != iter->tags.end();
          row.extensions.clear();
          row.extensions.reserve(iter->valid_strings.size());
          for (const auto& ext : iter->valid_strings)
          {
            row.extensions.emplace_back(ext);
          }
          row.keep_as_new_layer = true;
          output_rows_.push_back(std::move(row));
        }
      }
    }

    clear_widget_layout(input_mapping_widget_);
    clear_widget_layout(output_mapping_widget_);

    auto* input_layout = new QGridLayout(input_mapping_widget_);
    input_layout->setContentsMargins(0, 0, 0, 0);
    input_layout->addWidget(new QLabel("Layer"), 0, 0);
    input_layout->addWidget(new QLabel("Input parameter"), 0, 1);
    input_layout->addWidget(new QLabel("Extensions"), 0, 2);

    if (input_rows_.empty())
    {
      input_layout->addWidget(new QLabel("No input file parameters"), 1, 0, 1, 3);
    }
    else
    {
      for (Size i = 0; i < input_rows_.size(); ++i)
      {
        auto& row = input_rows_[i];
        row.layer_combo = new QComboBox(input_mapping_widget_);
        row.param_label = new QLabel(toQString(row.param_name), input_mapping_widget_);

        QStringList ext_labels;
        for (const auto& ext : row.extensions)
        {
          ext_labels << toQString(ext);
        }
        if (ext_labels.empty())
        {
          ext_labels << "<any>";
        }
        row.ext_label = new QLabel(ext_labels.join(", "), input_mapping_widget_);

        connect(row.layer_combo, CONNECTCAST(QComboBox, activated, (int)), this, [this, i](int)
        {
          auto& r = input_rows_[i];
          if (r.layer_combo->currentIndex() <= 0)
          {
            r.selected_layer = Size(-1);
          }
          else
          {
            r.selected_layer = static_cast<Size>(r.layer_combo->currentData().toULongLong());
          }
          refreshInputLayerCombos_();
        });

        input_layout->addWidget(row.layer_combo, static_cast<int>(i + 1), 0);
        input_layout->addWidget(row.param_label, static_cast<int>(i + 1), 1);
        input_layout->addWidget(row.ext_label, static_cast<int>(i + 1), 2);
      }

      refreshInputLayerCombos_();
    }

    auto* output_layout = new QGridLayout(output_mapping_widget_);
    output_layout->setContentsMargins(0, 0, 0, 0);
    output_layout->addWidget(new QLabel("Extensions"), 0, 0);
    output_layout->addWidget(new QLabel("Output parameter"), 0, 1);
    output_layout->addWidget(new QLabel("Handling"), 0, 2);

    if (output_rows_.empty())
    {
      output_layout->addWidget(new QLabel("No output file parameters"), 1, 0, 1, 3);
    }
    else
    {
      for (Size i = 0; i < output_rows_.size(); ++i)
      {
        auto& row = output_rows_[i];

        QStringList ext_labels;
        for (const auto& ext : row.extensions)
        {
          ext_labels << toQString(ext);
        }
        if (ext_labels.empty())
        {
          ext_labels << "<unknown>";
        }

        row.ext_label = new QLabel(ext_labels.join(", "), output_mapping_widget_);
        row.param_label = new QLabel(toQString(row.param_name), output_mapping_widget_);
        row.action_combo = new QComboBox(output_mapping_widget_);
        row.action_combo->addItem("new layer");
        row.action_combo->addItem("discard");
        row.action_combo->setCurrentIndex(row.keep_as_new_layer ? 0 : 1);

        connect(row.action_combo, CONNECTCAST(QComboBox, activated, (int)), this, [this, i](int)
        {
          output_rows_[i].keep_as_new_layer = output_rows_[i].action_combo->currentIndex() == 0;
        });

        output_layout->addWidget(row.ext_label, static_cast<int>(i + 1), 0);
        output_layout->addWidget(row.param_label, static_cast<int>(i + 1), 1);
        output_layout->addWidget(row.action_combo, static_cast<int>(i + 1), 2);
      }
    }
  }

  void ToolsDialog::refreshInputLayerCombos_()
  {
    if (canvas_ == nullptr)
    {
      return;
    }

    auto is_selected_elsewhere = [this](Size row_index, Size layer_index)
    {
      for (Size i = 0; i < input_rows_.size(); ++i)
      {
        if (i == row_index)
        {
          continue;
        }
        if (input_rows_[i].selected_layer == layer_index)
        {
          return true;
        }
      }
      return false;
    };

    for (Size i = 0; i < input_rows_.size(); ++i)
    {
      auto& row = input_rows_[i];
      if (row.layer_combo == nullptr)
      {
        continue;
      }

      row.layer_combo->blockSignals(true);
      row.layer_combo->clear();
      row.layer_combo->addItem("<select layer>", QVariant::fromValue<qulonglong>(qulonglong(-1)));

      for (const auto layer_index : row.eligible_layers)
      {
        if (is_selected_elsewhere(i, layer_index) && row.selected_layer != layer_index)
        {
          continue;
        }

        const auto& layer = canvas_->getLayer(layer_index);
        const QString label = QString("%1: %2").arg(static_cast<unsigned long long>(layer_index)).arg(toQString(layer.getName()));
        row.layer_combo->addItem(label, QVariant::fromValue<qulonglong>(static_cast<qulonglong>(layer_index)));
      }

      int selected_pos = row.layer_combo->findData(QVariant::fromValue<qulonglong>(static_cast<qulonglong>(row.selected_layer)));
      if (selected_pos < 0 && row.layer_combo->count() > 1)
      {
        selected_pos = 1;
      }
      row.layer_combo->setCurrentIndex(selected_pos >= 0 ? selected_pos : 0);

      if (row.layer_combo->currentIndex() > 0)
      {
        row.selected_layer = static_cast<Size>(row.layer_combo->currentData().toULongLong());
      }
      else
      {
        row.selected_layer = Size(-1);
      }

      row.layer_combo->blockSignals(false);
    }
  }

  std::vector<Size> ToolsDialog::findCompatibleLayers_(const std::vector<String>& extensions) const
  {
    std::vector<Size> compatible_layers;
    if (canvas_ == nullptr)
    {
      return compatible_layers;
    }

    std::vector<String> normalized_extensions;
    normalized_extensions.reserve(extensions.size());
    for (const auto& ext : extensions)
    {
      String normalized = ext;
      if (normalized.hasPrefix("*."))
      {
        normalized = normalized.substr(2);
      }
      normalized_extensions.push_back(normalized.toUpper());
    }

    for (Size i = 0; i < canvas_->getLayerCount(); ++i)
    {
      const String layer_ext = layerTypeToDefaultExtension_(canvas_->getLayer(i).type).toUpper();
      bool match = normalized_extensions.empty();
      for (const auto& ext : normalized_extensions)
      {
        if (ext == layer_ext)
        {
          match = true;
          break;
        }
      }

      if (match)
      {
        compatible_layers.push_back(i);
      }
    }
    return compatible_layers;
  }

  String ToolsDialog::layerTypeToDefaultExtension_(LayerDataBase::DataType type)
  {
    switch (type)
    {
      case LayerDataBase::DataType::DT_PEAK:
      case LayerDataBase::DataType::DT_CHROMATOGRAM:
        return FileTypes::typeToName(FileTypes::MZML);
      case LayerDataBase::DataType::DT_FEATURE:
        return FileTypes::typeToName(FileTypes::FEATUREXML);
      case LayerDataBase::DataType::DT_CONSENSUS:
        return FileTypes::typeToName(FileTypes::CONSENSUSXML);
      case LayerDataBase::DataType::DT_IDENT:
        return FileTypes::typeToName(FileTypes::IDXML);
      default:
        return FileTypes::typeToName(FileTypes::UNKNOWN);
    }
  }

  QStringList ToolsDialog::createToolsList_()
  {
    //Make sure the list is empty
    QStringList list;

    const auto& tools = ToolHandler::getTOPPToolList();
    plugin_params_ = tool_scanner_->getPluginParams();

    for (auto& pair : tools)
    {
      std::vector<LayerDataBase::DataType> tool_types = getTypesFromParam_(tool_params_.copy(pair.first + ':'));
      if (std::find(tool_types.begin(), tool_types.end(), layer_type_) != tool_types.end())
      {
        list << toQString(pair.first);
      }
    }
    //TODO: Plugins get added to the list just like tools and can't be differentiated in the GUI
    for (const auto& name : tool_scanner_->getPlugins())
    {
      std::vector<LayerDataBase::DataType> tool_types = getTypesFromParam_(plugin_params_.copy(name + ":"));
      if (std::find(tool_types.begin(), tool_types.end(), layer_type_) != tool_types.end())
      {
        list << toQString(String(name));
      }
    }

    //sort list alphabetically
    list.sort();
    list.push_front("<select tool>");
    return list;
  }

  void ToolsDialog::createINI_()
  {
    enable_();
    if (!arg_param_.empty())
    {
       tool_desc_->clear();
       arg_param_.clear();
       vis_param_.clear();
       editor_param_.clear();
       editor_->clear();
    }
    auto tool_name = getTool();
    arg_param_ = tool_params_.copy(tool_name + ":");
    if (arg_param_.empty())
    {
      arg_param_ = plugin_params_.copy(tool_name + ":");
    }

    tool_desc_->setText(toQString(String(arg_param_.getSectionDescription(tool_name))));
    vis_param_ = arg_param_.copy(tool_name + ":1:", true);
    
    // check if the tool has a threads parameter and show threads controls if yes
    has_threads_param_ = vis_param_.exists("threads");
    syncThreadsControlsFromVisParam_(true);
    updateThreadsControlsVisibility_();

    updateEditorParamFromVisParam_();

    editor_->load(editor_param_);

    setInputOutputCombo_(arg_param_);

    editor_->setFocus(Qt::MouseFocusReason);
  }

  void ToolsDialog::setTool_(int i)
  {
    editor_->clear();

    // no tool selected
    if (i == 0)
    {
      disable_();
      return;
    }

    createINI_();
  }

  void ToolsDialog::disable_()
  {
    ok_button_->setEnabled(false);
    if (input_mapping_widget_ != nullptr) input_mapping_widget_->setEnabled(false);
    if (output_mapping_widget_ != nullptr) output_mapping_widget_->setEnabled(false);
    has_threads_param_ = false;
    updateThreadsControlsVisibility_();
  }

  void ToolsDialog::enable_()
  {
    ok_button_->setEnabled(true);
    if (input_mapping_widget_ != nullptr) input_mapping_widget_->setEnabled(true);
    if (output_mapping_widget_ != nullptr) output_mapping_widget_->setEnabled(true);
  }

  void ToolsDialog::ok_()
  {
    if (tools_combo_->currentText() == "<select>")
    {
      QMessageBox::critical(this, "Error", "You have to select a tool.");
      return;
    }

    for (const auto& row : input_rows_)
    {
      if (row.required && row.selected_layer == Size(-1))
      {
        QMessageBox::critical(this, "Error", QString("Please select a layer for required input parameter '%1'.").arg(toQString(row.param_name)));
        return;
      }
    }

    editor_->store();
    mergeEditorParamIntoVisParam_();
    if (!applyThreadsToVisParam_())
    {
      return;
    }
    arg_param_.insert(getTool() + ":1:", vis_param_);
    if (!File::writable(ini_file_))
    {
      QMessageBox::critical(this, "Error", (String("Could not write to '") + ini_file_ + "'!").c_str());
    }
    ParamXMLFile paramFile;
    paramFile.store(ini_file_, arg_param_);
    accept();
  }

  void ToolsDialog::loadINI_()
  {
    QString string;
    filename_ = QFileDialog::getOpenFileName(this, tr("Open ini file"), default_dir_.c_str(), tr("ini files (*.ini);; all files (*.*)"));
    //not file selected
    if (filename_.isEmpty())
    {
      return;
    }
    enable_();
    if (!arg_param_.empty())
    {
      arg_param_.clear();
      vis_param_.clear();
      editor_param_.clear();
      editor_->clear();
    }
    try
    {
      ParamXMLFile paramFile;
      paramFile.load(filename_.toStdString(), arg_param_);
    }
    catch (Exception::BaseException& e)
    {
      QMessageBox::critical(this, "Error", QString("Error loading INI file: ") + e.what());
      arg_param_.clear();
      return;
    }
    //set tool combo
    Param::ParamIterator iter = arg_param_.begin();
    String str;
    string = iter.getName().substr(0, iter.getName().find(":")).c_str();
    Int pos = tools_combo_->findText(string);
    if (pos == -1)
    {
      QMessageBox::critical(this, "Error", (String("Cannot apply '") + fromQString(string) + "' tool to this layer type. Aborting!").c_str());
      arg_param_.clear();
      return;
    }
    tools_combo_->setCurrentIndex(pos);
    vis_param_ = arg_param_.copy(getTool() + ":1:", true);
    has_threads_param_ = vis_param_.exists("threads");
    syncThreadsControlsFromVisParam_(false);
    updateThreadsControlsVisibility_();
    updateEditorParamFromVisParam_();

    //load data into editor
    editor_->load(editor_param_);

    setInputOutputCombo_(arg_param_);
  }

  void ToolsDialog::storeINI_()
  {
    //nothing to save
    if (arg_param_.empty())
      return;

    filename_ = QFileDialog::getSaveFileName(this, tr("Save ini file"), default_dir_.c_str(), tr("ini files (*.ini)"));
    //not file selected
    if (filename_.isEmpty())
    {
      return;
    }

    if (!filename_.endsWith(".ini"))
    {
      filename_.append(".ini");
    }
    editor_->store();
    mergeEditorParamIntoVisParam_();

    if (!applyThreadsToVisParam_())
    {
      return;
    }

    arg_param_.insert(getTool() + ":1:", vis_param_);
    try
    {
      ParamXMLFile paramFile;
      paramFile.store(filename_.toStdString(), arg_param_);
    }
    catch (Exception::BaseException& e)
    {
      QMessageBox::critical(this, "Error", QString("Error storing INI file: ") + e.what());
      return;
    }
  }

  void ToolsDialog::reloadPlugins_()
  {
    QStringList list = createToolsList_();

    int32_t selected_index = list.indexOf(tools_combo_->currentText());

    if (selected_index < 1)
    {
      tool_desc_->clear();
      arg_param_.clear();
      vis_param_.clear();
      editor_param_.clear();
      editor_->clear();
      input_rows_.clear();
      output_rows_.clear();
      setInputOutputCombo_(arg_param_);
      has_threads_param_ = false;
      updateThreadsControlsVisibility_();
      disable_();
    }
    tools_combo_->clear();
    tools_combo_->addItems(list);
    if (selected_index > 0)
    {
      tools_combo_->setCurrentIndex(selected_index);
      createINI_();
    }
  }

  String ToolsDialog::getOutput()
  {
    if (output_rows_.empty())
    {
      return "";
    }
    return output_rows_.front().param_name;
  }

  String ToolsDialog::getInput()
  {
    if (input_rows_.empty())
    {
      return "";
    }
    return input_rows_.front().param_name;
  }

  String ToolsDialog::getTool()
  {
    return fromQString(tools_combo_->currentText());
  }

  String ToolsDialog::getExtension()
  {
    if (output_rows_.empty())
    {
      return FileTypes::typeToName(FileTypes::UNKNOWN);
    }

    auto valid_strings = output_rows_.front().extensions;
    if (valid_strings.size() == 1)
    {
      String extension = valid_strings[0];
      if (extension.hasPrefix("*."))
      {
        extension = extension.substr(2);
      }
      return extension;
    }

    return FileTypes::typeToName(FileTypes::UNKNOWN);
  }

  std::vector<std::pair<String, Size>> ToolsDialog::getInputLayerBindings() const
  {
    std::vector<std::pair<String, Size>> result;
    for (const auto& row : input_rows_)
    {
      if (row.selected_layer != Size(-1))
      {
        result.emplace_back(row.param_name, row.selected_layer);
      }
    }
    return result;
  }

  std::vector<std::tuple<String, bool, String, bool>> ToolsDialog::getOutputBindings() const
  {
    std::vector<std::tuple<String, bool, String, bool>> result;
    for (const auto& row : output_rows_)
    {
      String extension = FileTypes::typeToName(FileTypes::UNKNOWN);
      if (row.extensions.size() == 1)
      {
        extension = row.extensions[0];
        if (extension.hasPrefix("*."))
        {
          extension = extension.substr(2);
        }
      }

      bool keep_as_new_layer = row.keep_as_new_layer;
      if (row.action_combo != nullptr)
      {
        keep_as_new_layer = row.action_combo->currentIndex() == 0;
      }
      result.emplace_back(row.param_name, keep_as_new_layer, extension, row.required);
    }
    return result;
  }

  void ToolsDialog::updateEditorParamFromVisParam_()
  {
    editor_param_ = vis_param_;
    // parameters shown in dedicated GUI widgets and/or managed internally
    editor_param_.remove("log");
    editor_param_.remove("no_progress");
    editor_param_.remove("debug");
    editor_param_.remove("in");
    editor_param_.remove("out");
    editor_param_.remove("threads");
  }

  void ToolsDialog::mergeEditorParamIntoVisParam_()
  {
    vis_param_.update(editor_param_);
  }

  void ToolsDialog::initializeThreadsControls_()
  {
    // visual elements for threads parameter
    threads_widget_ = new QWidget(this);
    auto threads_layout = new QHBoxLayout(threads_widget_);
    threads_layout->setContentsMargins(0, 0, 0, 0);

    fast_mode_checkbox_ = new QCheckBox("FastMode", threads_widget_);
    fast_mode_checkbox_->setChecked(true);
    fast_mode_checkbox_->setToolTip("uses full system performance, may slow down other applications");

    threads_combo_ = new QComboBox(threads_widget_);

    // add options for 1 to max available threads in dropdown; max 8 threads
    if (max_threads_ <= 8)
    {
      for (int i = 1; i <= max_threads_; ++i)
      {
        threads_combo_->addItem(QString::number(i), i);
      }
    }
    else
    {
      // if we have more than 8 threads, add options for powers of 2 and max threads if its not a power of 2
      for (int i = 1; i <= max_threads_; i *= 2)
      {
        threads_combo_->addItem(QString::number(i), i);
      }
      if (threads_combo_->itemData(threads_combo_->count() - 1).toInt() != max_threads_)
      {
        threads_combo_->addItem(QString::number(max_threads_), max_threads_);
      }
    }

    threads_combo_->setCurrentIndex(max_threads_ - 1);
    threads_combo_->setToolTip("select custom threads number");

    threads_layout->addWidget(fast_mode_checkbox_);
    threads_layout->addStretch();
    threads_layout->addWidget(threads_combo_);
    
    connect(fast_mode_checkbox_, &QCheckBox::toggled, this, &ToolsDialog::fastModeToggled_);
    connect(threads_combo_, CONNECTCAST(QComboBox, activated, (int)), this, &ToolsDialog::manualThreadsComboChanged_);

    fastModeToggled_(true);
    threads_widget_->setVisible(false);
  }

  // check if the tool has a threads parameter and show threads controls if yes
  void ToolsDialog::updateThreadsControlsVisibility_()
  {
    if (threads_widget_ != nullptr)
    {
      threads_widget_->setVisible(has_threads_param_);
    }
    if (cpu_usage_label_ != nullptr)
    {
      cpu_usage_label_->setVisible(has_threads_param_);
    }
  }

  // synchronize manual controls and fast mode based on current vis_param_ value
  void ToolsDialog::syncThreadsControlsFromVisParam_(bool default_fast_mode)
  {
    if (!has_threads_param_ || fast_mode_checkbox_ == nullptr || threads_combo_ == nullptr)
    {
      return;
    }

    int threads = max_threads_;
    if (vis_param_.exists("threads"))
    {
      threads = clampThreadCount_(static_cast<int>(vis_param_.getValue("threads")));
    }

    if (default_fast_mode)
    {
      threads = max_threads_;
    }

    const bool fast_mode = default_fast_mode ? true : (threads == max_threads_);

    fast_mode_checkbox_->blockSignals(true);
    threads_combo_->blockSignals(true);

    fast_mode_checkbox_->setChecked(fast_mode);

    const int combo_index = threads_combo_->findData(threads);
    if (combo_index >= 0)
    {
      threads_combo_->setCurrentIndex(combo_index);
    }
    else
    {
      threads_combo_->setCurrentIndex(threads_combo_->count() - 1);
    }

    fast_mode_checkbox_->blockSignals(false);
    threads_combo_->blockSignals(false);

    fastModeToggled_(fast_mode);
  }

  bool ToolsDialog::applyThreadsToVisParam_()
  {
    if (!has_threads_param_)
    {
      return true;
    }

    int threads = max_threads_;
    if (!fast_mode_checkbox_->isChecked())
    {
      int requested = threads_combo_->currentData().toInt();
      if (requested <= 0)
      {
        requested = max_threads_;
      }
      threads = clampThreadCount_(requested);
      const int combo_index = threads_combo_->findData(threads);
      // if the value is not in the combo, select the max threads option
      threads_combo_->setCurrentIndex(combo_index >= 0 ? combo_index : (threads_combo_->count() - 1));
    }

    vis_param_.setValue("threads", threads);
    return true;
  }

  int ToolsDialog::clampThreadCount_(int value) const
  {
    if (value < 1)
    {
      return 1;
    }
    if (value > max_threads_)
    {
      return max_threads_;
    }
    return value;
  }

  void ToolsDialog::fastModeToggled_(bool checked)
  {
    if (threads_combo_ == nullptr)
    {
      return;
    }

    threads_combo_->setEnabled(!checked);
    if (checked)
    {
      const int combo_index = threads_combo_->findData(max_threads_);
      if (combo_index >= 0)
      {
        threads_combo_->setCurrentIndex(combo_index);
      }
    }
  }

  void ToolsDialog::manualThreadsComboChanged_(int index)
  {
    // if we are in fast mode or the combo is not properly initialized, ignore changes in the combo box
    if (threads_combo_ == nullptr || fast_mode_checkbox_ == nullptr || fast_mode_checkbox_->isChecked())
    {
      return;
    }
    
    const int value = threads_combo_->itemData(index).toInt();
    if (value < 1 || value > max_threads_)
    {
      const int fallback = threads_combo_->findData(max_threads_);
      if (fallback >= 0)
      {
        threads_combo_->setCurrentIndex(fallback);
      }
    }
  }

}
