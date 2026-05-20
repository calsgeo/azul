// azul
// Copyright © 2016-2026 Ken Arroyo Ohori
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef JSONParsingHelper_hpp
#define JSONParsingHelper_hpp

#include <any>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include "DataModel.hpp"
#include "simdjson.h"

class JSONParsingHelper {
protected:
  struct ParsedMaterial {
    bool hasDiffuseColor;
    float diffuseColor[3];
    bool hasTransparency;
    float transparency;

    ParsedMaterial() {
      hasDiffuseColor = false;
      hasTransparency = false;
      transparency = 0.0f;
      diffuseColor[0] = 0.0f;
      diffuseColor[1] = 0.0f;
      diffuseColor[2] = 0.0f;
    }
  };

  struct ParsedTexture {
    std::string imageUri;
  };

  struct AppearanceContext {
    std::vector<ParsedMaterial> materials;
    std::vector<ParsedTexture> textures;
    std::vector<std::array<float, 2>> textureVertices;
    std::string defaultThemeTexture;
    std::string defaultThemeMaterial;

    void clear() {
      materials.clear();
      textures.clear();
      textureVertices.clear();
      defaultThemeTexture.clear();
      defaultThemeMaterial.clear();
    }
  };

  std::string_view docType;
  std::string_view docVersion;
  std::vector<std::pair<std::string, size_t>> deferredParentRelationships;

  AppearanceContext appearanceContext;
  std::vector<AzulAppearanceStyle> stylePool;
  std::unordered_map<std::string, int> styleIdByKey;
  std::set<std::string> parsedThemes;
  std::string currentFilePath;

  std::string appearanceStyleKey(const AzulAppearanceStyle &style) {
    std::ostringstream key;
    key << (style.hasTexture ? "T" : "N") << "|"
        << style.theme << "|"
        << style.textureUri << "|"
        << (style.hasMaterial ? "M" : "N") << "|"
        << style.materialColour[0] << ","
        << style.materialColour[1] << ","
        << style.materialColour[2] << ","
        << style.materialColour[3];
    return key.str();
  }

  int addOrGetStyleId(const AzulAppearanceStyle &style) {
    std::string key = appearanceStyleKey(style);
    auto found = styleIdByKey.find(key);
    if (found != styleIdByKey.end()) return found->second;
    stylePool.push_back(style);
    int newId = static_cast<int>(stylePool.size()-1);
    styleIdByKey[key] = newId;
    return newId;
  }

  void resetAppearanceForNewFile() {
    appearanceContext.clear();
    stylePool.clear();
    styleIdByKey.clear();
    parsedThemes.clear();
  }

  void finalizeAppearanceForFile(AzulObject &parsedFile) {
    parsedFile.appearanceStyles = stylePool;
    parsedFile.appearanceThemes.assign(parsedThemes.begin(), parsedThemes.end());
  }

  std::string resolveImageUri(const std::string &imageUri) const {
    if (imageUri.empty()) return "";
    if (imageUri.find("://") != std::string::npos) return imageUri;
    if (imageUri[0] == '/') return imageUri;
    if (imageUri.size() > 1 && imageUri[1] == ':') return imageUri;
    std::filesystem::path sourcePath(currentFilePath);
    std::filesystem::path resolved = sourcePath.parent_path() / std::filesystem::path(imageUri);
    return resolved.lexically_normal().string();
  }

  bool isNumberValue(simdjson::ondemand::value value, unsigned long long &out) {
    if (value.type() != simdjson::ondemand::json_type::number) return false;
    auto result = value.get_uint64();
    if (result.error()) return false;
    out = static_cast<unsigned long long>(result.value_unsafe());
    return true;
  }

  bool isNumberValue(simdjson::ondemand::value value, double &out) {
    if (value.type() != simdjson::ondemand::json_type::number) return false;
    auto result = value.get_double();
    if (result.error()) return false;
    out = result.value_unsafe();
    return true;
  }

  bool isNumberValue(simdjson::simdjson_result<simdjson::ondemand::value> valueResult, unsigned long long &out) {
    if (valueResult.error()) return false;
    return isNumberValue(valueResult.value_unsafe(), out);
  }

  bool isNumberValue(simdjson::simdjson_result<simdjson::ondemand::value> valueResult, double &out) {
    if (valueResult.error()) return false;
    return isNumberValue(valueResult.value_unsafe(), out);
  }

  bool parseAnyArray(simdjson::ondemand::value value, std::vector<std::any> &out) {
    if (value.type() != simdjson::ondemand::json_type::array) return false;
    simdjson::ondemand::array jsonArray;
    if (value.get_array().get(jsonArray)) return false;
    parseNestedArray(jsonArray, out);
    return true;
  }

  bool parseColourTriplet(simdjson::ondemand::value value, float colour[3]) {
    if (value.type() != simdjson::ondemand::json_type::array) return false;
    simdjson::ondemand::array array;
    if (value.get_array().get(array)) return false;

    std::vector<double> values;
    for (auto entry: array) {
      double current = 0.0;
      if (!isNumberValue(entry, current)) return false;
      values.push_back(current);
    }
    if (values.size() != 3) return false;
    colour[0] = static_cast<float>(values[0]);
    colour[1] = static_cast<float>(values[1]);
    colour[2] = static_cast<float>(values[2]);
    return true;
  }

  void parseAppearanceObject(simdjson::ondemand::object appearanceObject) {
    appearanceContext.clear();

    simdjson::ondemand::array materialsArray;
    if (!appearanceObject["materials"].get(materialsArray)) {
      for (auto materialValue: materialsArray) {
        ParsedMaterial parsedMaterial;
        simdjson::ondemand::object materialObject;
        if (materialValue.get(materialObject)) {
          appearanceContext.materials.push_back(parsedMaterial);
          continue;
        }
        for (auto field: materialObject) {
          std::string_view key = field.unescaped_key().value();
          if (key == "diffuseColor") {
            float colour[3];
            if (parseColourTriplet(field.value(), colour)) {
              parsedMaterial.hasDiffuseColor = true;
              parsedMaterial.diffuseColor[0] = colour[0];
              parsedMaterial.diffuseColor[1] = colour[1];
              parsedMaterial.diffuseColor[2] = colour[2];
            }
          } else if (key == "transparency") {
            double transparency = 0.0;
            if (isNumberValue(field.value(), transparency)) {
              parsedMaterial.hasTransparency = true;
              parsedMaterial.transparency = static_cast<float>(transparency);
            }
          }
        }
        appearanceContext.materials.push_back(parsedMaterial);
      }
    }

    simdjson::ondemand::array texturesArray;
    if (!appearanceObject["textures"].get(texturesArray)) {
      for (auto textureValue: texturesArray) {
        ParsedTexture parsedTexture;
        simdjson::ondemand::object textureObject;
        if (textureValue.get(textureObject)) {
          appearanceContext.textures.push_back(parsedTexture);
          continue;
        }
        auto imageResult = textureObject["image"].get_string();
        if (!imageResult.error()) {
          parsedTexture.imageUri = resolveImageUri(std::string(imageResult.value_unsafe()));
        }
        appearanceContext.textures.push_back(parsedTexture);
      }
    }

    simdjson::ondemand::array uvVertices;
    if (!appearanceObject["vertices-texture"].get(uvVertices)) {
      for (auto uvValue: uvVertices) {
        simdjson::ondemand::array uvPair;
        if (uvValue.get(uvPair)) continue;
        std::vector<double> values;
        for (auto coord: uvPair) {
          double current = 0.0;
          if (!isNumberValue(coord, current)) {
            values.clear();
            break;
          }
          values.push_back(current);
        }
        if (values.size() == 2) {
          appearanceContext.textureVertices.push_back({
            static_cast<float>(values[0]),
            static_cast<float>(values[1])
          });
        }
      }
    }

    auto defaultTextureTheme = appearanceObject["default-theme-texture"].get_string();
    if (!defaultTextureTheme.error()) {
      appearanceContext.defaultThemeTexture = std::string(defaultTextureTheme.value_unsafe());
    }
    auto defaultMaterialTheme = appearanceObject["default-theme-material"].get_string();
    if (!defaultMaterialTheme.error()) {
      appearanceContext.defaultThemeMaterial = std::string(defaultMaterialTheme.value_unsafe());
    }
  }

  void parseAppearanceFromDocument(simdjson::ondemand::object docObject) {
    simdjson::ondemand::object appearanceObject;
    if (docObject["appearance"].get(appearanceObject)) {
      appearanceContext.clear();
      return;
    }
    parseAppearanceObject(appearanceObject);
  }

  bool anyAsVector(const std::any &value, std::vector<std::any> &asVector) {
    if (!value.has_value()) return false;
    auto casted = std::any_cast<std::vector<std::any>>(&value);
    if (casted == nullptr) return false;
    asVector = *casted;
    return true;
  }

  bool anyAsIndex(const std::any &value, unsigned long long &index) {
    if (!value.has_value()) return false;
    auto casted = std::any_cast<unsigned long long>(&value);
    if (casted == nullptr) return false;
    index = *casted;
    return true;
  }

  std::any copyAnyAt(const std::vector<std::any> &values, std::size_t index) {
    if (index >= values.size()) return std::any();
    return values[index];
  }

  bool parseGeometryThemeAssignments(simdjson::ondemand::object geometryObject,
                                     const char *fieldName,
                                     const std::string &defaultTheme,
                                     bool allowSingleValue,
                                     std::any &assignment,
                                     std::string &selectedTheme) {
    assignment = std::any();
    selectedTheme.clear();

    simdjson::ondemand::object assignmentsByTheme;
    if (geometryObject[fieldName].get(assignmentsByTheme)) return false;

    std::map<std::string, std::any> candidates;
    for (auto themeField: assignmentsByTheme) {
      std::string themeName = std::string(themeField.unescaped_key().value());
      simdjson::ondemand::object themeObject;
      if (themeField.value().get(themeObject)) continue;

      simdjson::ondemand::array valuesArray;
      if (!themeObject["values"].get(valuesArray)) {
        std::vector<std::any> nested;
        parseNestedArray(valuesArray, nested);
        candidates[themeName] = nested;
        continue;
      }

      if (allowSingleValue) {
        auto valueIndex = themeObject["value"].get_uint64();
        if (!valueIndex.error()) {
          candidates[themeName] = static_cast<unsigned long long>(valueIndex.value_unsafe());
        }
      }
    }

    if (candidates.empty()) return false;

    if (!defaultTheme.empty() && candidates.count(defaultTheme) > 0) {
      selectedTheme = defaultTheme;
      assignment = candidates[defaultTheme];
    } else {
      selectedTheme = candidates.begin()->first;
      assignment = candidates.begin()->second;
    }
    return true;
  }

  void collectPolygonPointers(AzulObject &object, std::vector<AzulPolygon *> &polygons) {
    for (auto &polygon: object.polygons) polygons.push_back(&polygon);
    for (auto &child: object.children) collectPolygonPointers(child, polygons);
  }

  void flattenRingVertexIndices(const std::vector<std::any> &boundaries,
                                int nesting,
                                std::vector<std::vector<std::vector<unsigned long long>>> &result) {
    if (nesting > 1) {
      for (const auto &entry: boundaries) {
        std::vector<std::any> child;
        if (!anyAsVector(entry, child)) continue;
        flattenRingVertexIndices(child, nesting-1, result);
      }
      return;
    }

    std::vector<std::vector<unsigned long long>> polygonRings;
    for (const auto &ringAny: boundaries) {
      std::vector<std::any> ring;
      if (!anyAsVector(ringAny, ring)) continue;
      std::vector<unsigned long long> indices;
      for (const auto &value: ring) {
        unsigned long long index = 0;
        if (anyAsIndex(value, index)) indices.push_back(index);
      }
      polygonRings.push_back(indices);
    }
    result.push_back(polygonRings);
  }

  void flattenNestedAssignments(const std::any &value,
                                int depth,
                                std::vector<std::any> &result,
                                bool propagateScalar = false) {
    if (depth <= 1) {
      result.push_back(value);
      return;
    }

    std::vector<std::any> asVector;
    if (anyAsVector(value, asVector)) {
      for (const auto &child: asVector) {
        flattenNestedAssignments(child, depth-1, result, propagateScalar);
      }
      return;
    }

    if (propagateScalar && value.has_value()) {
      result.push_back(value);
      return;
    }

    result.push_back(std::any());
  }

  bool parseTextureRingAssignment(const std::any &value,
                                  std::vector<std::vector<unsigned long long>> &textureAssignmentsByRing,
                                  std::vector<bool> &ringHasTexture) {
    textureAssignmentsByRing.clear();
    ringHasTexture.clear();

    std::vector<std::any> rings;
    if (!anyAsVector(value, rings)) return false;

    for (const auto &ringAny: rings) {
      std::vector<unsigned long long> ringValues;
      bool hasTexture = true;

      std::vector<std::any> ringAsVector;
      if (!anyAsVector(ringAny, ringAsVector)) {
        ringHasTexture.push_back(false);
        textureAssignmentsByRing.push_back(ringValues);
        continue;
      }

      for (const auto &item: ringAsVector) {
        unsigned long long current = 0;
        if (anyAsIndex(item, current)) {
          ringValues.push_back(current);
        } else {
          hasTexture = false;
          break;
        }
      }

      if (ringValues.empty()) hasTexture = false;
      ringHasTexture.push_back(hasTexture);
      textureAssignmentsByRing.push_back(ringValues);
    }

    return true;
  }

  bool assignTextureCoordinatesToRing(AzulRing &ring,
                                      const std::vector<unsigned long long> &ringVertices,
                                      const std::vector<unsigned long long> &ringTextureIndices) {
    if (ringTextureIndices.size() < 2) return false;
    std::size_t expected = ringVertices.size();
    if (ringTextureIndices.size() != expected + 1) return false;
    if (ring.points.size() != expected + 1) return false;

    ring.textureCoordinates.clear();
    ring.textureCoordinates.reserve(ring.points.size());

    for (std::size_t i = 1; i < ringTextureIndices.size(); ++i) {
      unsigned long long uvIndex = ringTextureIndices[i];
      if (uvIndex >= appearanceContext.textureVertices.size()) {
        ring.textureCoordinates.clear();
        ring.hasTextureCoordinates = false;
        return false;
      }
      ring.textureCoordinates.push_back(appearanceContext.textureVertices[uvIndex]);
    }

    if (!ring.textureCoordinates.empty()) {
      ring.textureCoordinates.push_back(ring.textureCoordinates.front());
      ring.hasTextureCoordinates = true;
      return true;
    }

    ring.hasTextureCoordinates = false;
    return false;
  }

  void applyGeometryAppearanceAssignments(simdjson::ondemand::object currentGeometry,
                                          const std::vector<std::any> &boundaries,
                                          int nesting,
                                          AzulObject &lodObject) {
    std::any materialAssignment;
    std::string materialTheme;
    bool hasMaterialAssignment = parseGeometryThemeAssignments(currentGeometry,
                                                               "material",
                                                               appearanceContext.defaultThemeMaterial,
                                                               true,
                                                               materialAssignment,
                                                               materialTheme);

    std::any textureAssignment;
    std::string textureTheme;
    bool hasTextureAssignment = parseGeometryThemeAssignments(currentGeometry,
                                                              "texture",
                                                              appearanceContext.defaultThemeTexture,
                                                              false,
                                                              textureAssignment,
                                                              textureTheme);

    if (!hasMaterialAssignment && !hasTextureAssignment) return;

    std::vector<AzulPolygon *> polygons;
    collectPolygonPointers(lodObject, polygons);
    if (polygons.empty()) return;

    std::vector<std::vector<std::vector<unsigned long long>>> polygonRings;
    flattenRingVertexIndices(boundaries, nesting, polygonRings);
    if (polygonRings.empty()) return;

    std::size_t polygonCount = std::min(polygons.size(), polygonRings.size());

    std::vector<std::any> materialPerPolygon;
    if (hasMaterialAssignment) {
      flattenNestedAssignments(materialAssignment, nesting-1, materialPerPolygon, true);
    }

    std::vector<std::any> texturePerPolygon;
    if (hasTextureAssignment) {
      flattenNestedAssignments(textureAssignment, nesting, texturePerPolygon, false);
    }

    for (std::size_t polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex) {
      AzulAppearanceStyle style;
      bool hasStyle = false;

      if (hasMaterialAssignment && polygonIndex < materialPerPolygon.size()) {
        unsigned long long materialIndex = 0;
        if (anyAsIndex(materialPerPolygon[polygonIndex], materialIndex) &&
            materialIndex < appearanceContext.materials.size()) {
          const ParsedMaterial &material = appearanceContext.materials[materialIndex];
          if (material.hasDiffuseColor) {
            style.hasMaterial = true;
            style.materialColour[0] = material.diffuseColor[0];
            style.materialColour[1] = material.diffuseColor[1];
            style.materialColour[2] = material.diffuseColor[2];
            float transparency = material.hasTransparency ? material.transparency : 0.0f;
            if (transparency < 0.0f) transparency = 0.0f;
            if (transparency > 1.0f) transparency = 1.0f;
            style.materialColour[3] = 1.0f-transparency;
            style.theme = materialTheme;
            hasStyle = true;
          }
        }
      }

      if (hasTextureAssignment && polygonIndex < texturePerPolygon.size()) {
        std::vector<std::vector<unsigned long long>> ringTextureAssignments;
        std::vector<bool> ringHasTexture;
        if (parseTextureRingAssignment(texturePerPolygon[polygonIndex], ringTextureAssignments, ringHasTexture)) {
          bool polygonHasTexture = true;
          unsigned long long textureIndexForPolygon = 0;
          bool textureIndexSet = false;

          std::vector<AzulRing *> polygonRingsTargets;
          polygonRingsTargets.push_back(&polygons[polygonIndex]->exteriorRing);
          for (auto &innerRing: polygons[polygonIndex]->interiorRings) {
            polygonRingsTargets.push_back(&innerRing);
          }

          if (ringTextureAssignments.size() != polygonRingsTargets.size() ||
              polygonRingsTargets.size() != polygonRings[polygonIndex].size()) {
            polygonHasTexture = false;
          }

          if (polygonHasTexture) {
            for (std::size_t ringIndex = 0; ringIndex < polygonRingsTargets.size(); ++ringIndex) {
              if (!ringHasTexture[ringIndex]) {
                polygonHasTexture = false;
                break;
              }
              const auto &ringAssignment = ringTextureAssignments[ringIndex];
              if (ringAssignment.empty()) {
                polygonHasTexture = false;
                break;
              }
              if (!textureIndexSet) {
                textureIndexForPolygon = ringAssignment[0];
                textureIndexSet = true;
              } else if (textureIndexForPolygon != ringAssignment[0]) {
                polygonHasTexture = false;
                break;
              }

              if (!assignTextureCoordinatesToRing(*polygonRingsTargets[ringIndex],
                                                  polygonRings[polygonIndex][ringIndex],
                                                  ringAssignment)) {
                polygonHasTexture = false;
                break;
              }
            }
          }

          if (polygonHasTexture && textureIndexSet && textureIndexForPolygon < appearanceContext.textures.size()) {
            const ParsedTexture &texture = appearanceContext.textures[textureIndexForPolygon];
            if (!texture.imageUri.empty()) {
              style.hasTexture = true;
              style.textureUri = texture.imageUri;
              if (style.theme.empty()) style.theme = textureTheme;
              hasStyle = true;
            }
          }
        }
      }

      if (hasStyle) {
        if (!style.theme.empty()) parsedThemes.insert(style.theme);
        int styleId = addOrGetStyleId(style);
        polygons[polygonIndex]->appearanceStyleId = styleId;
      }
    }
  }

  void parseCityJSONObject(simdjson::ondemand::object jsonObject, AzulObject &object, size_t childIdx, std::vector<std::tuple<double, double, double>> &vertices, AzulObject *geometryTemplates) {

    // Type (mandatory)
    try {
      object.type = jsonObject["type"].get_string().value();
    } catch (simdjson::simdjson_error &e) {
      std::cout << "no type specified" << std::endl;
      return;
    }

    // Geometry (optional)
    try {
      for (auto geometry: jsonObject["geometry"]) {
        parseCityJSONObjectGeometry(geometry.get_object(), object, vertices, geometryTemplates);
      }
    } catch (simdjson::simdjson_error &e) {
    }

    // Attributes (optional)
    {
      simdjson::ondemand::object attributesObject;
      if (!jsonObject["attributes"].get(attributesObject)) {
        for (auto attribute: attributesObject) {
          simdjson::ondemand::value attrValue = attribute.value();
          switch (attrValue.type()) {
            case simdjson::ondemand::json_type::string:
              object.attributes.push_back(std::pair<std::string, std::string>(attribute.unescaped_key().value(), attrValue.get_string().value()));
              break;
            case simdjson::ondemand::json_type::number:
              object.attributes.push_back(std::pair<std::string, std::string>(attribute.unescaped_key().value(), std::to_string(attrValue.get_double())));
              break;
            case simdjson::ondemand::json_type::boolean:
              if (attrValue.get_bool() == true) object.attributes.push_back(std::pair<std::string, std::string>(attribute.unescaped_key().value(), "true"));
              else object.attributes.push_back(std::pair<std::string, std::string>(attribute.unescaped_key().value(), "false"));
              break;
            case simdjson::ondemand::json_type::null:
              object.attributes.push_back(std::pair<std::string, std::string>(attribute.unescaped_key().value(), "null"));
              break;
            default:
              std::cout << attribute.unescaped_key().value() << ": unknown attribute type" << std::endl;
              break;
          }
        }
      }
    }

    // Parents (optional)
    {
      simdjson::ondemand::array parents;
      if (!jsonObject["parents"].get(parents)) {
        for (auto parent: parents) {
          deferredParentRelationships.emplace_back(std::string(parent.get_string().value()), childIdx);
        }
      }
    }

    // TODO: geographicalExtent
  }

  void parseCityJSONObjectGeometry(simdjson::ondemand::object currentGeometry, AzulObject &object, std::vector<std::tuple<double, double, double>> &vertices, AzulObject *geometryTemplates) {
    std::vector<std::map<std::string_view, std::string_view>> semanticSurfaces;
    std::string geometryType, geometryLod;
    std::vector<double> transformationMatrix;
    unsigned long long templateIndex;
    bool withSemantics = false;

    // Mandatory
    try {
      geometryType = currentGeometry["type"].get_string().value();
    } catch (simdjson::simdjson_error &e) {
      std::cout << "no geometry type specified" << std::endl;
      return;
    }

    try {
      switch (currentGeometry["lod"].type()) {
        case simdjson::ondemand::json_type::string:
          geometryLod = currentGeometry["lod"].get_string().value();
          break;
        case simdjson::ondemand::json_type::number:
          geometryLod = std::to_string(currentGeometry["lod"].get_double());
          break;
        default:
          std::cout << "unknown lod type" << std::endl;
          break;
      }
    } catch (simdjson::simdjson_error &e) {
      if (geometryType != "GeometryInstance") std::cout << "no LoD specified" << std::endl;
      geometryLod = "unknown";
    }

    std::vector<std::any> boundaries;
    parseNestedArray(currentGeometry["boundaries"].get_array(), boundaries);

    // Optional semantics
    std::vector<std::any> semantics;
    simdjson::ondemand::object element;
    auto error = currentGeometry["semantics"].get(element);
    if (!error) {
      withSemantics = true;
      for (simdjson::ondemand::object surface: element["surfaces"]) {
        semanticSurfaces.push_back(std::map<std::string_view, std::string_view>());
        for (auto attribute: surface) {
          simdjson::ondemand::value attrValue = attribute.value();
          switch (attrValue.type()) {
            case simdjson::ondemand::json_type::string:
              semanticSurfaces.back()[attribute.unescaped_key().value()] = attrValue.get_string().value();
              break;
            case simdjson::ondemand::json_type::number:
              semanticSurfaces.back()[attribute.unescaped_key().value()] = std::to_string(attrValue.get_double());
              break;
            case simdjson::ondemand::json_type::boolean:
              if (attrValue.get_bool() == true) semanticSurfaces.back()[attribute.unescaped_key().value()] = "true";
              else semanticSurfaces.back()[attribute.unescaped_key().value()] = "false";
              break;
            case simdjson::ondemand::json_type::null:
              semanticSurfaces.back()[attribute.unescaped_key().value()] = "null";
              break;
            default:
              std::cout << "unknown attribute type" << std::endl;
              break;
          }
        }
      }
      parseNestedArray(element["values"].get_array(), semantics);
    }

    error = currentGeometry["template"].get_uint64().get(templateIndex);
    if (error) templateIndex = 0;
    simdjson::ondemand::array transformationMatrixArray;
    error = currentGeometry["transformationMatrix"].get_array().get(transformationMatrixArray);
    if (!error) for (auto matrixElement: transformationMatrixArray) transformationMatrix.push_back(matrixElement.get_double().value());

    if (!geometryType.empty()) {
      std::any semanticsAsAny(semantics);

      if (geometryType == "MultiSurface" ||
          geometryType == "CompositeSurface") {
        object.children.push_back(AzulObject());
        object.children.back().type = "LoD";
        object.children.back().id = geometryLod;
        parseCityJSONGeometry(boundaries, semanticsAsAny, withSemantics, semanticSurfaces, 2, object.children.back(), vertices);
        applyGeometryAppearanceAssignments(currentGeometry, boundaries, 2, object.children.back());
      }

      else if (geometryType == "Solid") {
        object.children.push_back(AzulObject());
        object.children.back().type = "LoD";
        object.children.back().id = geometryLod;
        parseCityJSONGeometry(boundaries, semanticsAsAny, withSemantics, semanticSurfaces, 3, object.children.back(), vertices);
        applyGeometryAppearanceAssignments(currentGeometry, boundaries, 3, object.children.back());
      }

      else if (geometryType == "MultiSolid" ||
               geometryType == "CompositeSolid") {
        object.children.push_back(AzulObject());
        object.children.back().type = "LoD";
        object.children.back().id = geometryLod;
        parseCityJSONGeometry(boundaries, semanticsAsAny, withSemantics, semanticSurfaces, 4, object.children.back(), vertices);
        applyGeometryAppearanceAssignments(currentGeometry, boundaries, 4, object.children.back());
      }

      else if (geometryType == "GeometryInstance") {
        if (geometryTemplates != NULL && templateIndex < geometryTemplates->children.size() && transformationMatrix.size() == 16) {
          unsigned long long anchorPoint = std::any_cast<unsigned long long>(boundaries[0]);
          object.children.push_back(AzulObject(geometryTemplates->children[templateIndex]));
          for (auto &polygon: object.children.back().polygons) {
            for (auto &point: polygon.exteriorRing.points) {
              float homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
                                             transformationMatrix[13]*point.coordinates[1] +
                                             transformationMatrix[14]*point.coordinates[2] +
                                             transformationMatrix[15]);
              float x = (transformationMatrix[0]*point.coordinates[0] +
                         transformationMatrix[1]*point.coordinates[1] +
                         transformationMatrix[2]*point.coordinates[2] +
                         transformationMatrix[3])/homogeneousCoordinate + std::get<0>(vertices[anchorPoint]);
              float y = (transformationMatrix[4]*point.coordinates[0] +
                         transformationMatrix[5]*point.coordinates[1] +
                         transformationMatrix[6]*point.coordinates[2] +
                         transformationMatrix[7])/homogeneousCoordinate + std::get<1>(vertices[anchorPoint]);
              float z = (transformationMatrix[8]*point.coordinates[0] +
                         transformationMatrix[9]*point.coordinates[1] +
                         transformationMatrix[10]*point.coordinates[2] +
                         transformationMatrix[11])/homogeneousCoordinate + std::get<2>(vertices[anchorPoint]);
              point.coordinates[0] = x;
              point.coordinates[1] = y;
              point.coordinates[2] = z;
            }
            for (auto &ring: polygon.interiorRings) {
              for (auto &point: ring.points) {
                float homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
                                               transformationMatrix[13]*point.coordinates[1] +
                                               transformationMatrix[14]*point.coordinates[2] +
                                               transformationMatrix[15]);
                float x = (transformationMatrix[0]*point.coordinates[0] +
                           transformationMatrix[1]*point.coordinates[1] +
                           transformationMatrix[2]*point.coordinates[2] +
                           transformationMatrix[3])/homogeneousCoordinate + std::get<0>(vertices[anchorPoint]);
                float y = (transformationMatrix[4]*point.coordinates[0] +
                           transformationMatrix[5]*point.coordinates[1] +
                           transformationMatrix[6]*point.coordinates[2] +
                           transformationMatrix[7])/homogeneousCoordinate + std::get<1>(vertices[anchorPoint]);
                float z = (transformationMatrix[8]*point.coordinates[0] +
                           transformationMatrix[9]*point.coordinates[1] +
                           transformationMatrix[10]*point.coordinates[2] +
                           transformationMatrix[11])/homogeneousCoordinate + std::get<2>(vertices[anchorPoint]);
                point.coordinates[0] = x;
                point.coordinates[1] = y;
                point.coordinates[2] = z;
              }
            }
          }
        }
      }
    }
  }

  void parseNestedArray(simdjson::ondemand::array jsonNestedArray, std::vector<std::any> &nestedArray) {
    nestedArray.clear();
    for (auto array: jsonNestedArray) {
      switch (array.type()) {
        case simdjson::ondemand::json_type::array: {
          std::vector<std::any> newArray;
          parseNestedArray(array.get_array(), newArray);
          nestedArray.push_back(newArray);
          break;
        }
        case simdjson::ondemand::json_type::number:
          nestedArray.push_back((unsigned long long)array.get_uint64());
          break;
        case simdjson::ondemand::json_type::null:
          nestedArray.push_back(std::any());
          break;
        default:
          nestedArray.push_back(std::any());
          break;
      }
    }
  }

  void parseCityJSONGeometry(std::vector<std::any> &boundaries, std::any &semantics, bool withSemantics, std::vector<std::map<std::string_view, std::string_view>> &semanticSurfaces, int nesting, AzulObject &object, std::vector<std::tuple<double, double, double>> &vertices) {
    if (nesting > 1) {
      if (semantics.has_value()) {
        try {
          std::vector<std::any> semanticsAsVector = std::any_cast<std::vector<std::any>>(semantics);
          if (!semanticsAsVector.empty()) {
            auto boundary = boundaries.begin();
            auto semantic = semanticsAsVector.begin();
            while (boundary != boundaries.end() && semantic != semanticsAsVector.end()) {
              std::vector<std::any> boundaryAsVector = std::any_cast<std::vector<std::any>>(*boundary);
              parseCityJSONGeometry(boundaryAsVector, *semantic, true, semanticSurfaces, nesting-1, object, vertices);
              ++boundary;
              ++semantic;
            }
          } else {
            for (auto boundary: boundaries) {
              std::vector<std::any> boundaryAsVector = std::any_cast<std::vector<std::any>>(boundary);
              std::any empty;
              parseCityJSONGeometry(boundaryAsVector, empty, false, semanticSurfaces, nesting-1, object, vertices);
            }
          }
        } catch (const std::bad_any_cast &e) {}

        try {
          unsigned long long semanticsAsIndex = std::any_cast<unsigned long long>(semantics);
#pragma unused(semanticsAsIndex)
          for (auto boundary: boundaries) {
            std::vector<std::any> boundaryAsVector = std::any_cast<std::vector<std::any>>(boundary);
            parseCityJSONGeometry(boundaryAsVector, semantics, true, semanticSurfaces, nesting-1, object, vertices);
          }
        } catch (const std::bad_any_cast &e) {}
      }

      else {
        for (auto boundary: boundaries) {
          std::vector<std::any> boundaryAsVector = std::any_cast<std::vector<std::any>>(boundary);
          std::any empty;
          parseCityJSONGeometry(boundaryAsVector, empty, false, semanticSurfaces, nesting-1, object, vertices);
        }
      }
    }

    else if (nesting == 1) {
      if (withSemantics) {
        try {
          unsigned long long surfaceIndex = std::any_cast<unsigned long long>(semantics);
          if (surfaceIndex < semanticSurfaces.size()) {
            object.children.push_back(AzulObject());
            for (auto attribute: semanticSurfaces[surfaceIndex]) {
              if (attribute.first == "type") {
                object.children.back().type = attribute.second;
              } else object.children.back().attributes.push_back(std::pair<std::string, std::string>(attribute.first, attribute.second));
            }
            object.children.back().polygons.push_back(AzulPolygon());
            parseCityJSONPolygon(boundaries, object.children.back().polygons.back(), vertices);
          } else {
            object.polygons.push_back(AzulPolygon());
            parseCityJSONPolygon(boundaries, object.polygons.back(), vertices);
          }
        } catch (const std::bad_any_cast &e) {
          // null semantics entry => still keep geometry
          object.polygons.push_back(AzulPolygon());
          parseCityJSONPolygon(boundaries, object.polygons.back(), vertices);
        }
      } else {
        object.polygons.push_back(AzulPolygon());
        parseCityJSONPolygon(boundaries, object.polygons.back(), vertices);
      }
    }
  }

  void parseCityJSONPolygon(std::vector<std::any> &jsonPolygon, AzulPolygon &polygon, std::vector<std::tuple<double, double, double>> &vertices) {
    bool outer = true;
    for (auto ring: jsonPolygon) {
      try {
        std::vector<std::any> jsonRing = std::any_cast<std::vector<std::any>>(ring);
        if (outer) {
          parseCityJSONRing(jsonRing, polygon.exteriorRing, vertices);
          outer = false;
        } else {
          polygon.interiorRings.push_back(AzulRing());
          parseCityJSONRing(jsonRing, polygon.interiorRings.back(), vertices);
        }
      } catch (const std::bad_any_cast &e) {
        std::cout << "Ring is not an array" << std::endl;
      }
    }
  }

  void parseCityJSONRing(std::vector<std::any> &jsonRing, AzulRing &ring, std::vector<std::tuple<double, double, double>> &vertices) {
    for (auto jsonVertex: jsonRing) {
      try {
        unsigned long long vertexIndex = std::any_cast<unsigned long long>(jsonVertex);
        if (vertexIndex < vertices.size()) {
          ring.points.push_back(AzulPoint());
          ring.points.back().coordinates[0] = std::get<0>(vertices[vertexIndex]);
          ring.points.back().coordinates[1] = std::get<1>(vertices[vertexIndex]);
          ring.points.back().coordinates[2] = std::get<2>(vertices[vertexIndex]);
        }
      } catch (const std::bad_any_cast &e) {
        std::cout << "Vertex index is not an integer" << std::endl;
      }
    }
    if (!ring.points.empty()) ring.points.push_back(ring.points.front());
  }

  void buildHierarchy(AzulObject &parsedFile) {
    if (deferredParentRelationships.empty()) return;

    std::unordered_map<std::string, size_t> idToIndex;
    idToIndex.reserve(parsedFile.children.size());
    for (size_t i = 0; i < parsedFile.children.size(); ++i) {
      idToIndex[parsedFile.children[i].id] = i;
    }

    std::vector<std::vector<size_t>> parentToChildrenIndices(parsedFile.children.size());
    std::vector<uint8_t> isChild(parsedFile.children.size(), false);
    for (auto &[parentId, childIdx] : deferredParentRelationships) {
      auto parentIt = idToIndex.find(parentId);
      if (parentIt == idToIndex.end()) continue;
      parentToChildrenIndices[parentIt->second].push_back(childIdx);
      isChild[childIdx] = true;
    }

    std::vector<size_t> rootIndices;
    for (size_t i = 0; i < parsedFile.children.size(); ++i) {
      if (!isChild[i]) rootIndices.push_back(i);
    }

    std::vector<AzulObject> hierarchicalChildren;
    std::vector<uint8_t> moved(parsedFile.children.size(), false);
    hierarchicalChildren.reserve(rootIndices.size());

    std::vector<std::pair<std::vector<AzulObject>*, size_t>> stack;
    stack.reserve(parsedFile.children.size());

    for (size_t rootIdx : rootIndices) {
      moved[rootIdx] = true;
      hierarchicalChildren.push_back(std::move(parsedFile.children[rootIdx]));
      auto &root = hierarchicalChildren.back();
      root.children.reserve(parentToChildrenIndices[rootIdx].size());
      stack.emplace_back(&root.children, rootIdx);
    }

    while (!stack.empty()) {
      auto [slot, parentIdx] = stack.back();
      stack.pop_back();
      for (size_t childIdx : parentToChildrenIndices[parentIdx]) {
        if (moved[childIdx]) continue;
        moved[childIdx] = true;
        slot->push_back(std::move(parsedFile.children[childIdx]));
        auto &childChildren = slot->back().children;
        childChildren.reserve(parentToChildrenIndices[childIdx].size());
        stack.emplace_back(&childChildren, childIdx);
      }
    }

    parsedFile.children = std::move(hierarchicalChildren);
  }

public:
  std::string statusMessage;

  void parse(const char *filePath, AzulObject &parsedFile) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json;
    simdjson::ondemand::document doc;
    auto error = simdjson::padded_string::load(filePath).get(json);
    if (error) {
      std::cout << "Invalid file" << std::endl;
      return;
    }
    error = parser.iterate(json).get(doc);
    if (error) {
      std::cout << "Invalid JSON" << std::endl;
      return;
    }

    resetAppearanceForNewFile();
    currentFilePath = std::string(filePath);

    parsedFile.type = "File";
    parsedFile.id = filePath;

    if (doc.type() != simdjson::ondemand::json_type::object) return;
    for (auto element: doc.get_object()) {
      if (element.key().value().is_equal("type")) {
        docType = element.value().get_string();
      } else if (element.key().value().is_equal("version")) {
        docVersion = element.value().get_string();
      }
    }

    if (docType == "CityJSON") {
      std::cout << docType << " " << docVersion << " detected" << std::endl;
      if (docVersion == "1.0" ||
          docVersion == "1.1" ||
          docVersion == "2.0") {

        simdjson::ondemand::object object;

        // Metadata
        error = doc["metadata"].get(object);
        if (!error) {
          for (auto element: object) {
            std::string_view attributeName = element.unescaped_key();
            if (element.value().type() == simdjson::ondemand::json_type::string) {
              std::string_view attributeValue = element.value().get_string();
              parsedFile.attributes.push_back(std::pair<std::string, std::string>(attributeName, attributeValue));
            } else {
              std::cout << attributeName << " is a complex attribute. Skipped." << std::endl;
            }
          }
        }

        // Transform object
        std::vector<double> scale;
        std::vector<double> translation;
        error = doc["transform"].get(object);
        if (!error) {
          for (auto element: object) {
            if (element.key().value().is_equal("scale")) {
              for (auto axis: element.value()) {
                scale.push_back(axis.get_double().value());
              }
            } else if (element.key().value().is_equal("translate")) {
              for (auto axis: element.value()) {
                translation.push_back(axis.get_double().value());
              }
            }
          }
          if (scale.size() != 3) {
            scale.clear();
            for (int i = 0; i < 3; ++i) scale.push_back(1.0);
            std::cout << "Transform scale incorrect: set to " << scale[0] << ", " << scale[1] << ", " << scale[2] << std::endl;
          }
          if (translation.size() != 3) {
            translation.clear();
            for (int i = 0; i < 3; ++i) translation.push_back(0.0);
            std::cout << "Transform translation incorrect: set to " << translation[0] << ", " << translation[1] << ", " << translation[2] << std::endl;
          }
        } else {
          for (int i = 0; i < 3; ++i) scale.push_back(1.0);
          for (int i = 0; i < 3; ++i) translation.push_back(0.0);
        }

        // Appearance object
        simdjson::ondemand::object appearanceObject;
        error = doc["appearance"].get(appearanceObject);
        if (!error) {
          parseAppearanceObject(appearanceObject);
        } else {
          appearanceContext.clear();
        }

        // Geometry templates
        AzulObject geometryTemplates;
        std::vector<std::tuple<double, double, double>> geometryTemplatesVertices;
        error = doc["geometry-templates"].get(object);
        if (!error) {
          for (auto vertex: object["vertices-templates"].get_array()) {
            std::vector<double> coordinates;
            for (auto coordinate: vertex) coordinates.push_back(coordinate.get_double().value());
            if (coordinates.size() == 3) geometryTemplatesVertices.push_back(std::tuple<double, double, double>(coordinates[0], coordinates[1], coordinates[2]));
            else geometryTemplatesVertices.push_back(std::tuple<double, double, double>(0, 0, 0));
          }

          for (auto t: object["templates"].get_array()) {
            parseCityJSONObjectGeometry(t.get_object(), geometryTemplates, geometryTemplatesVertices, NULL);
          }
        }

        // Vertices
        std::vector<std::tuple<double, double, double>> vertices;
        for (auto vertex: doc["vertices"].get_array()) {
          std::vector<double> coordinates;
          for (auto coordinate: vertex) coordinates.push_back(coordinate.get_double().value());
          if (coordinates.size() == 3) vertices.push_back(std::tuple<double, double, double>(scale[0]*coordinates[0]+translation[0],
                                                                                             scale[1]*coordinates[1]+translation[1],
                                                                                             scale[2]*coordinates[2]+translation[2]));
          else vertices.push_back(std::tuple<double, double, double>(0, 0, 0));
        }

        // CityObjects
        deferredParentRelationships.clear();
        for (auto objectElement: doc["CityObjects"].get_object()) {
          parsedFile.children.push_back(AzulObject());
          std::string_view objectId = objectElement.unescaped_key();
          parsedFile.children.back().id = objectId;
          parseCityJSONObject(objectElement.value().get_object(), parsedFile.children.back(), parsedFile.children.size() - 1, vertices, &geometryTemplates);
        }
        buildHierarchy(parsedFile);

        finalizeAppearanceForFile(parsedFile);
        statusMessage = "Loaded CityJSON " + std::string(docVersion) + " file";
      } else {
        statusMessage = "CityJSON " + std::string(docVersion) + " is not supported";
      }
    } else {
      statusMessage = "JSON files other than CityJSON are not supported";
    }
  }

  void dump(const std::any &any) {
    if (any.has_value()) {
      try {
        std::cout << std::any_cast<unsigned long long>(any);
      } catch (const std::bad_any_cast &e) {}
      try {
        dump(std::any_cast<std::vector<std::any>>(any));
      } catch (const std::bad_any_cast &e) {}
      std::cout << " ";
    } else {
      std::cout << "null ";
    }
  }

  void dump(const std::vector<std::any> &list) {
    std::cout << "[";
    for (auto const &element: list) dump(element);
    std::cout << "]";
  }

  void clearDOM() {
    deferredParentRelationships.clear();
    appearanceContext.clear();
    stylePool.clear();
    styleIdByKey.clear();
    parsedThemes.clear();
    currentFilePath.clear();
  }
};

#endif /* JSONParsingHelper_hpp */
