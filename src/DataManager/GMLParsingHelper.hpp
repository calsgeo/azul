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

#ifndef GMLParsingHelper_hpp
#define GMLParsingHelper_hpp

#include "DataModel.hpp"

#include <array>
#include <boost/spirit/home/x3.hpp>
#include <filesystem>
#include <list>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <unordered_map>

class GMLParsingHelper {
  pugi::xml_document doc;
  std::vector<AzulAppearanceStyle> appearanceStyles;
  std::unordered_map<std::string, int> appearanceStyleIdByKey;
  std::unordered_map<std::string, int> appearanceStyleIdByPolygonId;
  std::unordered_map<std::string, int> appearanceStyleIdByRingId;
  std::unordered_map<std::string, std::vector<std::array<float, 2>>> ringTextureCoordinatesByRingId;
  std::set<std::string> appearanceThemes;
  std::set<std::string> activeGeometryXlinks;
  std::string currentFilePath;
  
  const char *typeWithoutNamespace(const char *type) {
    const char *namespaceSeparator = strchr(type, ':');
    if (namespaceSeparator != NULL) return namespaceSeparator+1;
    else return type;
  }

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
    auto found = appearanceStyleIdByKey.find(key);
    if (found != appearanceStyleIdByKey.end()) return found->second;
    appearanceStyles.push_back(style);
    int newId = static_cast<int>(appearanceStyles.size()-1);
    appearanceStyleIdByKey[key] = newId;
    return newId;
  }

  std::string resolveImageUri(const std::string &imageUri) const {
    if (imageUri.empty()) return "";
    if (imageUri.find("://") != std::string::npos) return imageUri;
    if (imageUri[0] == '/') return imageUri;
    if (imageUri.size() > 1 && imageUri[1] == ':') return imageUri;
    std::filesystem::path sourcePath(currentFilePath);
    std::filesystem::path resolved = (sourcePath.parent_path() / std::filesystem::path(imageUri)).lexically_normal();
    if (std::filesystem::exists(resolved)) return resolved.string();

    // Accept both common folder names used in datasets: "appearance" and "appearances".
    std::string normalized = imageUri;
    std::string altImageUri = imageUri;
    std::size_t pluralPos = normalized.find("appearances/");
    if (pluralPos != std::string::npos) {
      altImageUri.replace(pluralPos, std::string("appearances").size(), "appearance");
    } else {
      std::size_t singularPos = normalized.find("appearance/");
      if (singularPos != std::string::npos) {
        altImageUri.replace(singularPos, std::string("appearance").size(), "appearances");
      } else {
        return resolved.string();
      }
    }
    std::filesystem::path altResolved = (sourcePath.parent_path() / std::filesystem::path(altImageUri)).lexically_normal();
    if (std::filesystem::exists(altResolved)) return altResolved.string();
    return resolved.string();
  }

  std::string normaliseReference(std::string reference) {
    if (reference.empty()) return "";
    if (reference[0] == '#') reference.erase(reference.begin());
    std::size_t hashPos = reference.find('#');
    if (hashPos != std::string::npos) reference = reference.substr(hashPos+1);
    std::size_t startQuote = reference.find('\'');
    if (startQuote != std::string::npos) {
      std::size_t endQuote = reference.find('\'', startQuote+1);
      if (endQuote != std::string::npos && endQuote > startQuote+1) {
        reference = reference.substr(startQuote+1, endQuote-startQuote-1);
      }
    }
    return reference;
  }

  std::string extractReferenceFromNode(const pugi::xml_node &node) {
    for (auto const &attribute: node.attributes()) {
      const char *attributeType = typeWithoutNamespace(attribute.name());
      if (strcmp(attributeType, "href") == 0 || strcmp(attributeType, "uri") == 0) return normaliseReference(attribute.value());
    }
    const char *value = node.child_value();
    if (value != nullptr && strlen(value) > 0) return normaliseReference(value);
    return "";
  }

  std::string extractIdFromNode(const pugi::xml_node &node) {
    for (auto const &attribute: node.attributes()) {
      const char *attributeType = typeWithoutNamespace(attribute.name());
      if (strcmp(attributeType, "id") == 0) return attribute.value();
    }
    return "";
  }

  void collectPolygonIds(const pugi::xml_node &node,
                         std::vector<std::string> &polygonIds,
                         const std::unordered_map<std::string, pugi::xml_node> &nodesById,
                         std::set<std::string> &visitedReferences) {
    const char *nodeType = typeWithoutNamespace(node.name());
    if (strcmp(nodeType, "Polygon") == 0 || strcmp(nodeType, "Triangle") == 0) {
      std::string polygonId = extractIdFromNode(node);
      if (!polygonId.empty()) polygonIds.push_back(polygonId);
    }

    std::string reference = extractReferenceFromNode(node);
    if (!reference.empty() && visitedReferences.insert(reference).second) {
      auto referencedNode = nodesById.find(reference);
      if (referencedNode != nodesById.end()) {
        collectPolygonIds(referencedNode->second, polygonIds, nodesById, visitedReferences);
      }
    }

    for (auto const &child: node.children()) collectPolygonIds(child, polygonIds, nodesById, visitedReferences);
  }

  void assignStyleToTargetReference(const std::string &targetRef,
                                    int styleId,
                                    const std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    if (targetRef.empty()) return;
    auto targetNode = nodesById.find(targetRef);
    if (targetNode == nodesById.end()) return;

    std::vector<std::string> polygonIds;
    std::set<std::string> visitedReferences;
    visitedReferences.insert(targetRef);
    collectPolygonIds(targetNode->second, polygonIds, nodesById, visitedReferences);
    for (auto const &polygonId: polygonIds) appearanceStyleIdByPolygonId[polygonId] = styleId;
  }

  bool parseReferencedNode(const std::string &reference,
                           AzulObject &parsedObject,
                           std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    if (reference.empty()) return false;
    if (!activeGeometryXlinks.insert(reference).second) return false;
    auto foundNode = nodesById.find(reference);
    if (foundNode != nodesById.end()) {
      parseCityGMLObject(foundNode->second, parsedObject, nodesById);
      activeGeometryXlinks.erase(reference);
      return true;
    }
    activeGeometryXlinks.erase(reference);
    return false;
  }

  bool parseDoubleList(const char *values, std::vector<double> &result) {
    result.clear();
    if (values == nullptr) return false;
    while (isspace(*values)) ++values;
    while (strlen(values) > 0) {
      const char *last = values;
      while (!isspace(*last) && *last != '\0') ++last;
      double parsedValue = 0.0;
      if (!boost::spirit::x3::parse(values, last, boost::spirit::x3::double_, parsedValue)) return false;
      result.push_back(parsedValue);
      values = last;
      while (isspace(*values)) ++values;
    }
    return true;
  }
  
  void buildNodesIndex(const pugi::xml_node &node, std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    for (auto const &attribute: node.attributes()) {
      const char *attributeType = typeWithoutNamespace(attribute.name());
      if (strcmp(attributeType, "id") == 0) nodesById[attribute.value()] = node;
      else if (strcmp(attributeType, "href") == 0) {
        const char *nodeType = typeWithoutNamespace(node.name());
        if (strcmp(nodeType, "relativeGMLGeometry") == 0 ||
            
            strcmp(nodeType, "appearance") == 0 ||
            strcmp(nodeType, "appearanceMember") == 0 ||
            strcmp(nodeType, "baseSurface") == 0 ||
            strcmp(nodeType, "curveMember") == 0 ||
            strcmp(nodeType, "curveMembers") == 0 ||
            strcmp(nodeType, "element") == 0 ||
            strcmp(nodeType, "exterior") == 0 ||
            strcmp(nodeType, "geometryMember") == 0 ||
            strcmp(nodeType, "interior") == 0 ||
            strcmp(nodeType, "patches") == 0||
            strcmp(nodeType, "pointMember") == 0 ||
            strcmp(nodeType, "pointMembers") == 0 ||
            strcmp(nodeType, "referencePoint") == 0 ||
            strcmp(nodeType, "segments") == 0 ||
            strcmp(nodeType, "solidMember") == 0 ||
            strcmp(nodeType, "solidMembers") == 0 ||
            strcmp(nodeType, "surfaceDataMember") == 0 ||
            strcmp(nodeType, "surfaceMember") == 0 ||
            strcmp(nodeType, "surfaceMembers") == 0 ||
            strcmp(nodeType, "target") == 0 ||
            strcmp(nodeType, "trianglePatches") == 0) {
        } else {
          std::cout << "Xlinked " << nodeType << std::endl;
        }
      }
    } for (auto const &child: node.children()) buildNodesIndex(child, nodesById);
  }

  void parseX3DMaterial(const pugi::xml_node &materialNode, std::unordered_map<std::string, pugi::xml_node> &nodesById, const std::string &theme) {
    AzulAppearanceStyle style;
    style.hasMaterial = true;
    style.theme = theme;
    style.materialColour[0] = 0.75f;
    style.materialColour[1] = 0.75f;
    style.materialColour[2] = 0.75f;
    style.materialColour[3] = 1.0f;

    for (auto const &child: materialNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "diffuseColor") == 0) {
        std::vector<double> values;
        if (parseDoubleList(child.child_value(), values) && values.size() >= 3) {
          style.materialColour[0] = static_cast<float>(values[0]);
          style.materialColour[1] = static_cast<float>(values[1]);
          style.materialColour[2] = static_cast<float>(values[2]);
        }
      } else if (strcmp(childType, "transparency") == 0) {
        std::vector<double> values;
        if (parseDoubleList(child.child_value(), values) && !values.empty()) {
          float transparency = static_cast<float>(values[0]);
          if (transparency < 0.0f) transparency = 0.0f;
          if (transparency > 1.0f) transparency = 1.0f;
          style.materialColour[3] = 1.0f-transparency;
        }
      }
    }

    int styleId = addOrGetStyleId(style);
    if (!theme.empty()) appearanceThemes.insert(theme);

    for (auto const &child: materialNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "target") != 0) continue;
      std::string targetRef = extractReferenceFromNode(child);
      if (targetRef.empty()) continue;
      assignStyleToTargetReference(targetRef, styleId, nodesById);
    }
  }

  void parseTexCoordList(const pugi::xml_node &texCoordListNode) {
    std::vector<std::pair<std::string, std::vector<std::array<float, 2>>>> parsedTextureCoordinates;
    for (auto const &child: texCoordListNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "textureCoordinates") != 0) continue;
      std::string ringRef;
      for (auto const &attribute: child.attributes()) {
        const char *attributeType = typeWithoutNamespace(attribute.name());
        if (strcmp(attributeType, "ring") == 0) {
          ringRef = normaliseReference(attribute.value());
          break;
        }
      }
      if (ringRef.empty()) continue;
      std::vector<double> values;
      if (!parseDoubleList(child.child_value(), values) || values.size() < 2 || values.size()%2 != 0) continue;
      std::vector<std::array<float, 2>> ringTextureCoordinates;
      ringTextureCoordinates.reserve(values.size()/2);
      for (std::size_t i = 0; i+1 < values.size(); i += 2) {
        ringTextureCoordinates.push_back({static_cast<float>(values[i]), static_cast<float>(values[i+1])});
      }
      parsedTextureCoordinates.push_back(std::make_pair(ringRef, ringTextureCoordinates));
    }
    for (auto const &entry: parsedTextureCoordinates) ringTextureCoordinatesByRingId[entry.first] = entry.second;
  }

  void assignStyleToTextureCoordinateRings(const pugi::xml_node &node, int styleId) {
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "textureCoordinates") == 0) {
        std::string ringRef;
        for (auto const &attribute: child.attributes()) {
          const char *attributeType = typeWithoutNamespace(attribute.name());
          if (strcmp(attributeType, "ring") == 0) {
            ringRef = normaliseReference(attribute.value());
            break;
          }
        }
        if (!ringRef.empty()) appearanceStyleIdByRingId[ringRef] = styleId;
      } else if (strcmp(childType, "TexCoordList") == 0 || strcmp(childType, "_TextureParameterization") == 0) {
        assignStyleToTextureCoordinateRings(child, styleId);
      }
    }
  }

  void parseParameterizedTexture(const pugi::xml_node &textureNode, std::unordered_map<std::string, pugi::xml_node> &nodesById, const std::string &theme) {
    AzulAppearanceStyle style;
    style.hasTexture = true;
    style.theme = theme;

    for (auto const &child: textureNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "imageURI") == 0) {
        style.textureUri = resolveImageUri(std::string(child.child_value()));
      }
    }
    if (style.textureUri.empty()) return;
    int styleId = addOrGetStyleId(style);
    if (!theme.empty()) appearanceThemes.insert(theme);

    for (auto const &child: textureNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "target") != 0) continue;

      std::string targetRef = extractReferenceFromNode(child);
      if (!targetRef.empty()) {
        assignStyleToTargetReference(targetRef, styleId, nodesById);
      }

      for (auto const &targetChild: child.children()) {
        const char *targetChildType = typeWithoutNamespace(targetChild.name());
        if (strcmp(targetChildType, "TexCoordList") == 0) {
          parseTexCoordList(targetChild);
          assignStyleToTextureCoordinateRings(targetChild, styleId);
        } else if (strcmp(targetChildType, "_TextureParameterization") == 0) {
          for (auto const &parameterizationChild: targetChild.children()) {
            const char *parameterizationType = typeWithoutNamespace(parameterizationChild.name());
            if (strcmp(parameterizationType, "TexCoordList") == 0) {
              parseTexCoordList(parameterizationChild);
              assignStyleToTextureCoordinateRings(parameterizationChild, styleId);
            }
          }
        }
      }
    }
  }

  void parseAppearanceNodes(const pugi::xml_node &node, std::unordered_map<std::string, pugi::xml_node> &nodesById, const std::string &inheritedTheme = "") {
    const char *nodeType = typeWithoutNamespace(node.name());
    std::string theme = inheritedTheme;

    if (strcmp(nodeType, "Appearance") == 0) {
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());
        if (strcmp(childType, "theme") == 0) theme = child.child_value();
      }
    }

    if (strcmp(nodeType, "X3DMaterial") == 0) {
      parseX3DMaterial(node, nodesById, theme);
    } else if (strcmp(nodeType, "ParameterizedTexture") == 0) {
      parseParameterizedTexture(node, nodesById, theme);
    }

    for (auto const &child: node.children()) parseAppearanceNodes(child, nodesById, theme);
  }
  
  void parseRing(const pugi::xml_node &node, AzulRing &parsedRing, std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    pugi::xml_node ringNode = node.first_child();
    if (!ringNode) {
      std::string ringReference = extractReferenceFromNode(node);
      if (!ringReference.empty()) {
        auto referencedRingNode = nodesById.find(ringReference);
        if (referencedRingNode != nodesById.end()) ringNode = referencedRingNode->second;
      }
    }
    if (!ringNode) return;

    std::string ringId = extractIdFromNode(ringNode);
    for (auto const &child: ringNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "pos") == 0 ||
          strcmp(childType, "posList") == 0) {

        const char *coordinates = child.child_value();
        while (isspace(*coordinates)) ++coordinates;
        unsigned int currentCoordinate = 0;
        while (strlen(coordinates) > 0) {
          if (currentCoordinate == 0) parsedRing.points.push_back(AzulPoint());
          const char *last = coordinates;
//          std::cout << "\"" << coordinates << "\"" << std::endl;
          while (!isspace(*last) && *last != '\0') ++last;
          if (!boost::spirit::x3::parse(coordinates, last, boost::spirit::x3::double_, parsedRing.points.back().coordinates[currentCoordinate])) {
            std::cout << "Invalid points: " << coordinates << ". Skipping..." << std::endl;
            parsedRing.points.clear();
            break;
          }
//          std::cout << "\t->" << parsedRing.points.back().coordinates[currentCoordinate] << std::endl;
          coordinates = last;
          while (isspace(*coordinates)) ++coordinates;
          currentCoordinate = (currentCoordinate+1)%3;
        } if (currentCoordinate != 0) {
          std::cout << "Wrong number of coordinates: not divisible by 3" << std::endl;
          parsedRing.points.clear();
        } //std::cout << "Created " << points.size() << " points" << std::endl;
      }
    }
    auto foundTextureCoordinates = ringTextureCoordinatesByRingId.find(ringId);
    if (foundTextureCoordinates != ringTextureCoordinatesByRingId.end()) {
      parsedRing.textureCoordinates = foundTextureCoordinates->second;
      if (parsedRing.points.size() == parsedRing.textureCoordinates.size()+1 &&
          parsedRing.points.size() >= 2 &&
          parsedRing.points.front().coordinates[0] == parsedRing.points.back().coordinates[0] &&
          parsedRing.points.front().coordinates[1] == parsedRing.points.back().coordinates[1] &&
          parsedRing.points.front().coordinates[2] == parsedRing.points.back().coordinates[2] &&
          !parsedRing.textureCoordinates.empty()) {
        parsedRing.textureCoordinates.push_back(parsedRing.textureCoordinates.front());
      } else if (parsedRing.textureCoordinates.size() == parsedRing.points.size()+1 &&
                 parsedRing.textureCoordinates.size() >= 2 &&
                 parsedRing.textureCoordinates.front()[0] == parsedRing.textureCoordinates.back()[0] &&
                 parsedRing.textureCoordinates.front()[1] == parsedRing.textureCoordinates.back()[1]) {
        parsedRing.textureCoordinates.pop_back();
      }
      parsedRing.hasTextureCoordinates = !parsedRing.textureCoordinates.empty();
    }

  }
  
  void parsePolygon(const pugi::xml_node &node, AzulPolygon &parsedPolygon, std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    pugi::xml_node polygonNode = node;
    if (!polygonNode.first_child()) {
      std::string polygonReference = extractReferenceFromNode(node);
      if (!polygonReference.empty()) {
        auto referencedPolygonNode = nodesById.find(polygonReference);
        if (referencedPolygonNode != nodesById.end()) polygonNode = referencedPolygonNode->second;
      }
    }

    std::string polygonId = extractIdFromNode(polygonNode);
    auto foundStyle = appearanceStyleIdByPolygonId.find(polygonId);
    if (foundStyle != appearanceStyleIdByPolygonId.end()) parsedPolygon.appearanceStyleId = foundStyle->second;

    for (auto const &child: polygonNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "exterior") == 0) {
        parseRing(child, parsedPolygon.exteriorRing, nodesById);
      } else if (strcmp(childType, "interior") == 0) {
        AzulRing ring;
        parseRing(child, ring, nodesById);
        parsedPolygon.interiorRings.push_back(ring);
      }
    }

    if (parsedPolygon.appearanceStyleId < 0) {
      pugi::xml_node exteriorNode;
      for (auto const &child: polygonNode.children()) {
        if (strcmp(typeWithoutNamespace(child.name()), "exterior") == 0) {
          exteriorNode = child;
          break;
        }
      }
      pugi::xml_node linearRingNode = exteriorNode.first_child();
      if (!linearRingNode) {
        std::string ringReference = extractReferenceFromNode(exteriorNode);
        if (!ringReference.empty()) {
          auto referencedRingNode = nodesById.find(ringReference);
          if (referencedRingNode != nodesById.end()) linearRingNode = referencedRingNode->second;
        }
      }
      std::string ringId = extractIdFromNode(linearRingNode);
      if (!ringId.empty()) {
        auto foundStyleByRing = appearanceStyleIdByRingId.find(ringId);
        if (foundStyleByRing != appearanceStyleIdByRingId.end()) parsedPolygon.appearanceStyleId = foundStyleByRing->second;
      }
    }
  }
  
  void parseGML(const pugi::xml_node &node, AzulObject &parsedObject) {
//    std::cout << "Node: \"" << node.name() << "\"" << std::endl;
    const char *nodeType = typeWithoutNamespace(node.name());
    std::string docType;
    std::string docVersion;
    
    // CityGML
    if (strcmp(nodeType, "CityModel") == 0) {
      for (auto const &attribute: node.attributes()) {
//        std::cout << attribute.name() << ": " << attribute.value() << std::endl;
        if (strncmp(attribute.name(), "xmlns", 5) == 0) {
          if (strcmp(attribute.value(), "http://www.opengis.net/citygml/1.0") == 0) {
            docType = "CityGML";
            docVersion = "1.0";
          } else if (strcmp(attribute.value(), "http://www.opengis.net/citygml/2.0") == 0) {
            docType = "CityGML";
            docVersion = "2.0";
          } else if (strcmp(attribute.value(), "http://www.opengis.net/citygml/3.0") == 0) {
            docType = "CityGML";
            docVersion = "3.0";
          }
        }
      } if (strcmp(docType.c_str(), "CityGML") == 0) {
        std::cout << docType << " " << docVersion << " detected" << std::endl;
        if (strcmp(docVersion.c_str(), "1.0") == 0 ||
            strcmp(docVersion.c_str(), "2.0") == 0) {
          std::unordered_map<std::string, pugi::xml_node> nodesById;
          std::cout << "Building nodes index...";
          buildNodesIndex(node, nodesById);
          std::cout << " done (" << nodesById.size() << " entries)." << std::endl;
          parseAppearanceNodes(node, nodesById);
          parseCityGMLObject(node, parsedObject, nodesById);
          parsedObject.appearanceStyles = appearanceStyles;
          parsedObject.appearanceThemes.assign(appearanceThemes.begin(), appearanceThemes.end());
          statusMessage = "Loaded CityGML " + docVersion + " file";
        } else {
          statusMessage = "CityGML " + docVersion + " is not supported, please upgrade to CityJSON";
        }
      }
    } 
    
    // IndoorGML
    else if (strcmp(nodeType, "IndoorFeatures") == 0) {
      for (auto const &attribute: node.attributes()) {
//        std::cout << attribute.name() << ": " << attribute.value() << std::endl;
        if (strncmp(attribute.name(), "xmlns", 5) == 0) {
          if (strcmp(attribute.value(), "http://www.opengis.net/indoorgml/1.0/core") == 0) {
            docType = "IndoorGML";
            docVersion = "1.0";
          }
        }
      } if (strcmp(docType.c_str(), "IndoorGML") == 0) {
        std::cout << docType << " " << docVersion << " detected" << std::endl;
        if (strcmp(docVersion.c_str(), "1.0") == 0) {
          std::unordered_map<std::string, pugi::xml_node> nodesById;
          std::cout << "Building nodes index...";
          buildNodesIndex(node, nodesById);
          std::cout << " done." << std::endl;
          parseIndoorGMLObject(node, parsedObject, nodesById);
          statusMessage = "Loaded IndoorGML " + docVersion + " file";
        }
      }
    }
    
    // Unknown yet -> continue with children
    else for (auto const &child: node.children()) parseGML(child, parsedObject);
  }
  
  void parseIndoorGMLObject(const pugi::xml_node &node, AzulObject &parsedObject, std::unordered_map<std::string, pugi::xml_node> &nodesById) {
    //    std::cout << "Node: \"" << node.name() << "\"" << std::endl;

    // Get rid of namespaces
    const char *nodeType = typeWithoutNamespace(node.name());

    // Objects: create in hierachy and parse attributes
    if (strcmp(nodeType, "CellSpace") == 0) {
      AzulObject newChild;
      newChild.type = nodeType;
      for (auto const &attribute: node.attributes()) {
        const char *attributeType = typeWithoutNamespace(attribute.name());
        if (strcmp(attributeType, "id") == 0) newChild.id = attribute.value();
      } for (auto const &child: node.children()) {
        parseIndoorGMLObject(child, newChild, nodesById);
      } parsedObject.children.push_back(newChild);
    }

    // Geometry
    else if (strcmp(nodeType, "Polygon") == 0 ||
             strcmp(nodeType, "Rectangle") == 0 ||
             strcmp(nodeType, "Triangle") == 0) {
      AzulPolygon polygon;
      parsePolygon(node, polygon, nodesById);
      parsedObject.polygons.push_back(polygon);
    }

    // Objects to flatten
    else {
      for (auto const &child: node.children()) parseIndoorGMLObject(child, parsedObject, nodesById);
    }
  }
  
  void parseCityGMLObject(const pugi::xml_node &node, AzulObject &parsedObject, std::unordered_map<std::string, pugi::xml_node> &nodesById) {

    // Get rid of namespaces
    const char *nodeType = typeWithoutNamespace(node.name());
//    std::cout << "Node: \"" << nodeType << "\"" << std::endl;
    
    // Ignored types
    if (strcmp(nodeType, "address") == 0 || // Complex type
        strcmp(nodeType, "appearance") == 0 ||  // Unsupported
        strcmp(nodeType, "appearanceMember") == 0 ||  // Unsupported
        strcmp(nodeType, "extent") == 0 ||  // Would cover other geometries, maybe render as edges later?
        strcmp(nodeType, "externalReference") == 0 || // Complex type
        strcmp(nodeType, "generalizesTo") == 0 || // Circular reference
        strcmp(nodeType, "genericAttributeSet") == 0 || // Complex type
        strcmp(nodeType, "measureAttribute") == 0 || // Complex type (but maybe just append units?)
        strcmp(nodeType, "parent") == 0 || // Circular reference
        strcmp(nodeType, "Envelope") == 0) {  // Would cover other geometries, maybe render as edges later?
    }
    
    // Put attributes in same object and parse children
    else if (strcmp(nodeType, "CityModel") == 0) {
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());
        std::size_t numberOfChildren = std::distance(child.children().begin(), child.children().end());
        
        if (numberOfChildren == 1) {
          std::size_t numberOfGrandChildren = std::distance(child.first_child().children().begin(), child.first_child().children().end());
          if (numberOfGrandChildren == 0) {
            if (strlen(child.first_child().value()) > 0) {
              parsedObject.attributes.push_back(std::pair<std::string, std::string>(childType, child.first_child().value()));
            }
          } else parseCityGMLObject(child, parsedObject, nodesById);
        } else if (numberOfChildren > 1) parseCityGMLObject(child, parsedObject, nodesById);
      }
    }
    
    // Custom attributes
    else if (strcmp(nodeType, "stringAttribute") == 0 ||
             strcmp(nodeType, "intAttribute") == 0 ||
             strcmp(nodeType, "doubleAttribute") == 0 ||
             strcmp(nodeType, "dateAttribute") == 0 ||
             strcmp(nodeType, "uriAttribute") == 0) {
      const char *name = node.attribute("name").value();
      const char *value = node.first_child().child_value();
      parsedObject.attributes.push_back(std::pair<std::string, std::string>(name, value));
    }
    
    // Objects to flatten (not useful in hierarchy)
    else if (strcmp(nodeType, "auxiliaryTrafficArea") == 0 || // Redundant elements from CityGML
             strcmp(nodeType, "boundedBy") == 0 ||
             strcmp(nodeType, "breaklines") == 0 ||
             strcmp(nodeType, "bridgeRoomInstallation") == 0 ||
             strcmp(nodeType, "cityObjectMember") == 0 ||
             strcmp(nodeType, "consistsOfBridgePart") == 0 ||
             strcmp(nodeType, "consistsOfBuildingPart") == 0 ||
             strcmp(nodeType, "consistsOfTunnelPart") == 0 ||
             strcmp(nodeType, "grid") == 0 ||
             strcmp(nodeType, "groupMember") == 0 ||
             strcmp(nodeType, "hollowSpaceInstallation") == 0 ||
             strcmp(nodeType, "interiorBridgeInstallation") == 0 ||
             strcmp(nodeType, "interiorBridgeRoom") == 0 ||
             strcmp(nodeType, "interiorBuildingInstallation") == 0 ||
             strcmp(nodeType, "interiorFurniture") == 0 ||
             strcmp(nodeType, "interiorHollowSpace") == 0 ||
             strcmp(nodeType, "interiorRoom") == 0 ||
             strcmp(nodeType, "interiorTunnelInstallation") == 0 ||
             strcmp(nodeType, "opening") == 0 ||
             strcmp(nodeType, "outerBridgeConstruction") == 0 ||
             strcmp(nodeType, "outerBridgeInstallation") == 0 ||
             strcmp(nodeType, "outerBuildingInstallation") == 0 ||
             strcmp(nodeType, "outerTunnelInstallation") == 0 ||
             strcmp(nodeType, "reliefComponent") == 0 ||
             strcmp(nodeType, "reliefPoints") == 0 ||
             strcmp(nodeType, "ridgeOrValleyLines") == 0 ||
             strcmp(nodeType, "roomInstallation") == 0 ||
             strcmp(nodeType, "tin") == 0 ||
             strcmp(nodeType, "trafficArea") == 0 ||
             
             strcmp(nodeType, "CompositeCurve") == 0 || // Geometry types (not necessary to show)
             strcmp(nodeType, "CompositeSolid") == 0 ||
             strcmp(nodeType, "CompositeSurface") == 0 ||
             strcmp(nodeType, "Curve") == 0 ||
             strcmp(nodeType, "GeometricComplex") == 0 ||
             strcmp(nodeType, "LineString") == 0 ||
             strcmp(nodeType, "MultiCurve") == 0 ||
             strcmp(nodeType, "MultiPoint") == 0 ||
             strcmp(nodeType, "MultiGeometry") == 0 ||
             strcmp(nodeType, "MultiSolid") == 0 ||
             strcmp(nodeType, "MultiSurface") == 0 ||
             strcmp(nodeType, "OrientableCurve") == 0 ||
             strcmp(nodeType, "OrientableSurface") == 0 ||
             strcmp(nodeType, "Shell") == 0 ||
             strcmp(nodeType, "Solid") == 0 ||
             strcmp(nodeType, "Surface") == 0 ||
             strcmp(nodeType, "TIN") == 0 ||
             strcmp(nodeType, "TriangulatedSurface") == 0) {
      for (auto const &child: node.children()) parseCityGMLObject(child, parsedObject, nodesById);
    }
    
    // Objects to flatten (not useful in hierarchy), representing redundant info from GML, but with potential xlinks
    else if (strcmp(nodeType, "baseSurface") == 0 ||
             strcmp(nodeType, "curveMember") == 0 ||
             strcmp(nodeType, "curveMembers") == 0 ||
             strcmp(nodeType, "element") == 0 ||
             strcmp(nodeType, "exterior") == 0 ||
             strcmp(nodeType, "geometryMember") == 0 ||
             strcmp(nodeType, "interior") == 0 ||
             strcmp(nodeType, "patches") == 0||
             strcmp(nodeType, "pointMember") == 0 ||
             strcmp(nodeType, "pointMembers") == 0 ||
             strcmp(nodeType, "segments") == 0 ||
             strcmp(nodeType, "solidMember") == 0 ||
             strcmp(nodeType, "solidMembers") == 0 ||
             strcmp(nodeType, "surfaceMember") == 0 ||
             strcmp(nodeType, "surfaceMembers") == 0 ||
             strcmp(nodeType, "trianglePatches") == 0) {
      for (auto const &child: node.children()) parseCityGMLObject(child, parsedObject, nodesById);
      const char *xlink = NULL;
      for (auto const &attribute: node.attributes()) {
        const char *attributeType = typeWithoutNamespace(attribute.name());
        if (strcmp(attributeType, "href") == 0) xlink = attribute.value();
      } if (xlink != NULL) {
        if (xlink[0] == '#') ++xlink;
        if (!parseReferencedNode(xlink, parsedObject, nodesById)) {
          std::cout << "Geometry with xlink " << xlink << " not found. Skipped." << std::endl;
        }
      }
    }
    
    // Objects to put in hierarchy
    else if (strcmp(nodeType, "BreaklineRelief") == 0 || // Relief
             strcmp(nodeType, "MassPointRelief") == 0 ||
             strcmp(nodeType, "RasterRelief") == 0 ||
             strcmp(nodeType, "ReliefFeature") == 0 ||
             strcmp(nodeType, "TINRelief") == 0 ||
             
             strcmp(nodeType, "Building") == 0 || // Building
             strcmp(nodeType, "BuildingFurniture") == 0 ||
             strcmp(nodeType, "BuildingInstallation") == 0 ||
             strcmp(nodeType, "BuildingPart") == 0 ||
             strcmp(nodeType, "IntBuildingInstallation") == 0 ||
             strcmp(nodeType, "Room") == 0 ||
             
             strcmp(nodeType, "HollowSpace") == 0 || // Tunnel
             strcmp(nodeType, "IntTunnelInstallation") == 0 ||
             strcmp(nodeType, "RoofSurface") == 0 ||
             strcmp(nodeType, "Tunnel") == 0 ||
             strcmp(nodeType, "TunnelInstallation") == 0 ||
             strcmp(nodeType, "TunnelFurniture") == 0 ||
             strcmp(nodeType, "TunnelPart") == 0 ||
             
             strcmp(nodeType, "Bridge") == 0 || // Bridge
             strcmp(nodeType, "BridgeConstructionElement") == 0 ||
             strcmp(nodeType, "BridgeFurniture") == 0 ||
             strcmp(nodeType, "BridgeInstallation") == 0 ||
             strcmp(nodeType, "BridgePart") == 0 ||
             strcmp(nodeType, "BridgeRoom") == 0 ||
             strcmp(nodeType, "IntBridgeInstallation") == 0 ||
             
             strcmp(nodeType, "WaterBody") == 0 || // WaterBody
             strcmp(nodeType, "WaterClosureSurface") == 0 ||
             strcmp(nodeType, "WaterGroundSurface") == 0 ||
             strcmp(nodeType, "WaterSurface") == 0 ||
             
             strcmp(nodeType, "AuxiliaryTrafficArea") == 0 || // Transportation
             strcmp(nodeType, "Railway") == 0 ||
             strcmp(nodeType, "Road") == 0 ||
             strcmp(nodeType, "Square") == 0 ||
             strcmp(nodeType, "Track") == 0 ||
             strcmp(nodeType, "TrafficArea") == 0 ||
             strcmp(nodeType, "TransportationComplex") == 0 ||
             
             strcmp(nodeType, "PlantCover") == 0 || // Vegetation
             strcmp(nodeType, "SolitaryVegetationObject") == 0 ||
             
             strcmp(nodeType, "CityFurniture") == 0 || // CityFurniture
             
             strcmp(nodeType, "LandUse") == 0 || // LandUse
             
             strcmp(nodeType, "CityObjectGroup") == 0 || // CityObjectGroup
             
             strcmp(nodeType, "GenericCityObject") == 0 || // GenericCityObject
             
             strcmp(nodeType, "CeilingSurface") == 0 || // Surface types for Building, Bridge and Tunnel
             strcmp(nodeType, "ClosureSurface") == 0 ||
             strcmp(nodeType, "Door") == 0 ||
             strcmp(nodeType, "FloorSurface") == 0 ||
             strcmp(nodeType, "GroundSurface") == 0 ||
             strcmp(nodeType, "InteriorWallSurface") == 0 ||
             strcmp(nodeType, "RoofSurface") == 0 ||
             strcmp(nodeType, "OuterCeilingSurface") == 0 ||
             strcmp(nodeType, "OuterFloorSurface") == 0 ||
             strcmp(nodeType, "WallSurface") == 0 ||
             strcmp(nodeType, "Window") == 0 ||
             
             strcmp(nodeType, "geometry") == 0 || // Geometry types (in case of multiple and to know which LoD is used)
             strcmp(nodeType, "lod0FootPrint") == 0 ||
             strcmp(nodeType, "lod1FootPrint") == 0 ||
             strcmp(nodeType, "lod2FootPrint") == 0 ||
             strcmp(nodeType, "lod3FootPrint") == 0 ||
             strcmp(nodeType, "lod4FootPrint") == 0 ||
             strcmp(nodeType, "lod0Geometry") == 0 ||
             strcmp(nodeType, "lod1Geometry") == 0 ||
             strcmp(nodeType, "lod2Geometry") == 0 ||
             strcmp(nodeType, "lod3Geometry") == 0 ||
             strcmp(nodeType, "lod4Geometry") == 0 ||
             strcmp(nodeType, "lod0ImplicitRepresentation") == 0 ||
             strcmp(nodeType, "lod1ImplicitRepresentation") == 0 ||
             strcmp(nodeType, "lod2ImplicitRepresentation") == 0 ||
             strcmp(nodeType, "lod3ImplicitRepresentation") == 0 ||
             strcmp(nodeType, "lod4ImplicitRepresentation") == 0 ||
             strcmp(nodeType, "lod0MultiCurve") == 0 ||
             strcmp(nodeType, "lod1MultiCurve") == 0 ||
             strcmp(nodeType, "lod2MultiCurve") == 0 ||
             strcmp(nodeType, "lod3MultiCurve") == 0 ||
             strcmp(nodeType, "lod4MultiCurve") == 0 ||
             strcmp(nodeType, "lod0MultiSolid") == 0 ||
             strcmp(nodeType, "lod1MultiSolid") == 0 ||
             strcmp(nodeType, "lod2MultiSolid") == 0 ||
             strcmp(nodeType, "lod3MultiSolid") == 0 ||
             strcmp(nodeType, "lod4MultiSolid") == 0 ||
             strcmp(nodeType, "lod0MultiSurface") == 0 ||
             strcmp(nodeType, "lod1MultiSurface") == 0 ||
             strcmp(nodeType, "lod2MultiSurface") == 0 ||
             strcmp(nodeType, "lod3MultiSurface") == 0 ||
             strcmp(nodeType, "lod4MultiSurface") == 0 ||
             strcmp(nodeType, "lod0Network") == 0 ||
             strcmp(nodeType, "lod1Network") == 0 ||
             strcmp(nodeType, "lod2Network") == 0 ||
             strcmp(nodeType, "lod3Network") == 0 ||
             strcmp(nodeType, "lod4Network") == 0 ||
             strcmp(nodeType, "lod0TerrainIntersection") == 0 ||
             strcmp(nodeType, "lod1TerrainIntersection") == 0 ||
             strcmp(nodeType, "lod2TerrainIntersection") == 0 ||
             strcmp(nodeType, "lod3TerrainIntersection") == 0 ||
             strcmp(nodeType, "lod4TerrainIntersection") == 0 ||
             strcmp(nodeType, "lod0RoofEdge") == 0 ||
             strcmp(nodeType, "lod1RoofEdge") == 0 ||
             strcmp(nodeType, "lod2RoofEdge") == 0 ||
             strcmp(nodeType, "lod3RoofEdge") == 0 ||
             strcmp(nodeType, "lod4RoofEdge") == 0 ||
             strcmp(nodeType, "lod0Solid") == 0 ||
             strcmp(nodeType, "lod1Solid") == 0 ||
             strcmp(nodeType, "lod2Solid") == 0 ||
             strcmp(nodeType, "lod3Solid") == 0 ||
             strcmp(nodeType, "lod4Solid") == 0 ||
             strcmp(nodeType, "lod0Surface") == 0 ||
             strcmp(nodeType, "lod1Surface") == 0 ||
             strcmp(nodeType, "lod2Surface") == 0 ||
             strcmp(nodeType, "lod3Surface") == 0 ||
             strcmp(nodeType, "lod4Surface") == 0) {

      AzulObject newChild;
      newChild.type = nodeType;
      for (auto const &attribute: node.attributes()) {
        const char *attributeType = typeWithoutNamespace(attribute.name());
        if (strcmp(attributeType, "id") == 0) newChild.id = attribute.value();
      }
      
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());
        std::size_t numberOfChildren = std::distance(child.children().begin(), child.children().end());
        
        if (numberOfChildren == 1) {
          std::size_t numberOfGrandChildren = std::distance(child.first_child().children().begin(), child.first_child().children().end());
          if (numberOfGrandChildren == 0) {
            if (strlen(child.first_child().value()) > 0) {
              newChild.attributes.push_back(std::pair<std::string, std::string>(childType, child.first_child().value()));
            }
          } else parseCityGMLObject(child, newChild, nodesById);
        } else if (numberOfChildren > 1) parseCityGMLObject(child, newChild, nodesById);
      }
      
      parsedObject.children.push_back(newChild);
    }
    
    // Explicit geometry
    else if (strcmp(nodeType, "Polygon") == 0 ||
             strcmp(nodeType, "Triangle") == 0) {
      AzulPolygon polygon;
      parsePolygon(node, polygon, nodesById);
      parsedObject.polygons.push_back(polygon);
    }
    
    // Implicit geometry
    else if (strcmp(nodeType, "ImplicitGeometry") == 0) {
//      std::cout << "Implicit geometry" << std::endl;
      std::vector<double> transformationMatrix;
      std::vector<double> anchorPointCoordinates;
      
      AzulObject transformedChild;
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());
        
        if (strcmp(childType, "transformationMatrix") == 0) {
          const char *values = child.child_value();
          while (isspace(*values)) ++values;
          while (strlen(values) > 0) {
            const char *last = values;
            while (!isspace(*last) && *last != '\0') ++last;
            double parsedValue;
            if (!boost::spirit::x3::parse(values, last, boost::spirit::x3::double_, parsedValue)) {
              std::cout << "Invalid value: " << values << ". Skipping..." << std::endl;
            } else {
              transformationMatrix.push_back(parsedValue);
            }
            values = last;
            while (isspace(*values)) ++values;
          }
        }
        
        else if (strcmp(childType, "relativeGMLGeometry") == 0) {
          for (auto const &grandchild: child.children()) parseCityGMLObject(grandchild, transformedChild, nodesById);
          const char *xlink = NULL;
          for (auto const &attribute: child.attributes()) {
            const char *attributeType = typeWithoutNamespace(attribute.name());
            if (strcmp(attributeType, "href") == 0) xlink = attribute.value();
          } if (xlink != NULL) {
            if (xlink[0] == '#') ++xlink;
            if (!parseReferencedNode(xlink, transformedChild, nodesById)) {
              std::cout << "Geometry with xlink " << xlink << " not found" << std::endl;
            }
          }
        }
        
        else if (strcmp(childType, "referencePoint") == 0) {
          for (auto const &point: child.children()) {
            const char *pointType = typeWithoutNamespace(point.name());
            if (strcmp(pointType, "Point") == 0) {
              for (auto const &pos: point.children()) {
                const char *posType = typeWithoutNamespace(pos.name());
                if (strcmp(posType, "pos") == 0) {
                  const char *coordinates = pos.child_value();
                  while (isspace(*coordinates)) ++coordinates;
                  while (strlen(coordinates) > 0) {
                    const char *last = coordinates;
                    while (!isspace(*last) && *last != '\0') ++last;
                    anchorPointCoordinates.push_back(0.0);
                    if (!boost::spirit::x3::parse(coordinates, last, boost::spirit::x3::double_, anchorPointCoordinates.back())) {
                      std::cout << "Invalid coordinates: " << coordinates << ". Skipping..." << std::endl;
                    } coordinates = last;
                    while (isspace(*coordinates)) ++coordinates;
                  } 
                }
              }
            }
          }
        }
      }
      
      if (transformationMatrix.size() == 16 && anchorPointCoordinates.size() == 3) {
//        std::cout << "Transformation matrix:";
//        for (auto const &value: transformationMatrix) std::cout << " " << value;
//        std::cout << std::endl;
        for (auto const &polygon: transformedChild.polygons) {
          parsedObject.polygons.push_back(AzulPolygon());
          for (auto const &point: polygon.exteriorRing.points) {
            parsedObject.polygons.back().exteriorRing.points.push_back(AzulPoint());
//            std::cout << "Point: " << point.coordinates[0] << " " << point.coordinates[1] << " " << point.coordinates[2] << std::endl;
            double homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
                                            transformationMatrix[13]*point.coordinates[1] +
                                            transformationMatrix[14]*point.coordinates[2] +
                                            transformationMatrix[15]);
            parsedObject.polygons.back().exteriorRing.points.back().coordinates[0] = (transformationMatrix[0]*point.coordinates[0] +
                                                                                      transformationMatrix[1]*point.coordinates[1] +
                                                                                      transformationMatrix[2]*point.coordinates[2] +
                                                                                      transformationMatrix[3])/homogeneousCoordinate + anchorPointCoordinates[0];
            parsedObject.polygons.back().exteriorRing.points.back().coordinates[1] = (transformationMatrix[4]*point.coordinates[0] +
                                                                                      transformationMatrix[5]*point.coordinates[1] +
                                                                                      transformationMatrix[6]*point.coordinates[2] +
                                                                                      transformationMatrix[7])/homogeneousCoordinate + anchorPointCoordinates[1];
            parsedObject.polygons.back().exteriorRing.points.back().coordinates[2] = (transformationMatrix[8]*point.coordinates[0] +
                                                                                      transformationMatrix[9]*point.coordinates[1] +
                                                                                      transformationMatrix[10]*point.coordinates[2] +
                                                                                      transformationMatrix[11])/homogeneousCoordinate + anchorPointCoordinates[2];
          } for (auto const &ring: polygon.interiorRings) {
            parsedObject.polygons.back().interiorRings.push_back(AzulRing());
            for (auto const &point: ring.points) {
              parsedObject.polygons.back().interiorRings.back().points.push_back(AzulPoint());
              double homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
                                              transformationMatrix[13]*point.coordinates[1] +
                                              transformationMatrix[14]*point.coordinates[2] +
                                              transformationMatrix[15]);
              parsedObject.polygons.back().interiorRings.back().points.back().coordinates[0] = (transformationMatrix[0]*point.coordinates[0] +
                                                                                                transformationMatrix[1]*point.coordinates[1] +
                                                                                                transformationMatrix[2]*point.coordinates[2] +
                                                                                                transformationMatrix[3])/homogeneousCoordinate;
              parsedObject.polygons.back().interiorRings.back().points.back().coordinates[1] = (transformationMatrix[4]*point.coordinates[0] +
                                                                                                transformationMatrix[5]*point.coordinates[1] +
                                                                                                transformationMatrix[6]*point.coordinates[2] +
                                                                                                transformationMatrix[7])/homogeneousCoordinate;
              parsedObject.polygons.back().interiorRings.back().points.back().coordinates[2] = (transformationMatrix[8]*point.coordinates[0] +
                                                                                                transformationMatrix[9]*point.coordinates[1] +
                                                                                                transformationMatrix[10]*point.coordinates[2] +
                                                                                                transformationMatrix[11])/homogeneousCoordinate;
            }
          }
        }
      } else std::cout << "Wrong size of transformation matrix: not 4x4" << std::endl;
    }
    
    else {
      std::cout << "Unknown node: \"" << node.name() << "\"" << std::endl;
      pugi::xml_node currentNode = node;
      std::list<std::string> hierarchy;
      while (currentNode.type() != pugi::node_null) {
        hierarchy.push_front(currentNode.name());
        currentNode = currentNode.parent();
      } std::cout << "  hierarchy:";
      for (auto const &currentName: hierarchy) std::cout << " -> " << currentName;
      std::cout << std::endl;
    }
  }
  
public:
  std::string statusMessage;
  
  void parse(const char *filePath, AzulObject &parsedFile) {
    parsedFile.type = "File";
    parsedFile.id = filePath;
    currentFilePath = filePath;
    appearanceStyles.clear();
    appearanceStyleIdByKey.clear();
    appearanceStyleIdByPolygonId.clear();
    appearanceStyleIdByRingId.clear();
    ringTextureCoordinatesByRingId.clear();
    appearanceThemes.clear();
    activeGeometryXlinks.clear();
    doc.load_file(filePath);
    parseGML(doc.root(), parsedFile);
    pugi::xpath_node srsNode = doc.select_node("//*[@srsName]");
    if (srsNode) {
      parsedFile.crsIdentifier = srsNode.node().attribute("srsName").value();
      std::cout << "CRS: " << parsedFile.crsIdentifier << std::endl;
    }
  }
  
  void clearDOM() {
    doc.reset();
    appearanceStyles.clear();
    appearanceStyleIdByKey.clear();
    appearanceStyleIdByPolygonId.clear();
    appearanceStyleIdByRingId.clear();
    ringTextureCoordinatesByRingId.clear();
    appearanceThemes.clear();
    activeGeometryXlinks.clear();
    currentFilePath.clear();
  }
};

#endif /* GMLParsingHelper_hpp */
