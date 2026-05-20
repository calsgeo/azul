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

#include <boost/spirit/home/x3.hpp>
#include <pugixml.hpp>
#include <array>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

class GMLParsingHelper {
  pugi::xml_document doc;

  struct ParsedTextureTarget {
    std::string textureUri;
    std::string theme;
    std::unordered_map<std::string, std::vector<std::array<float, 2>>> ringTextureCoordinates;
  };

  struct ParsedMaterial {
    bool hasDiffuseColor;
    bool hasTransparency;
    std::string theme;
    float diffuseColor[3];
    float transparency;
    ParsedMaterial() {
      hasDiffuseColor = false;
      hasTransparency = false;
      theme = "";
      diffuseColor[0] = 0.0f;
      diffuseColor[1] = 0.0f;
      diffuseColor[2] = 0.0f;
      transparency = 0.0f;
    }
  };

  std::unordered_map<std::string, ParsedTextureTarget> textureBySurfaceId;
  std::unordered_map<std::string, std::string> textureSurfaceIdByRingId;
  std::unordered_map<std::string, ParsedMaterial> materialBySurfaceId;
  std::set<std::string> appearanceThemes;
  std::unordered_map<std::string, std::string> namespaceUriByPrefix;
  std::unordered_map<std::string, std::vector<std::string>> xsdFilesByNamespace;
  std::unordered_map<std::string, std::unordered_set<std::string>> adeObjectNamesByNamespace;
  std::unordered_set<std::string> matchedADENamespaces;
  std::unordered_set<std::string> unmatchedADENamespaces;
  std::string currentFilePath;

  const char *typeWithoutNamespace(const char *type) const {
    const char *namespaceSeparator = strchr(type, ':');
    if (namespaceSeparator != NULL) return namespaceSeparator+1;
    else return type;
  }

  std::string namespacePrefix(const char *type) const {
    const char *namespaceSeparator = strchr(type, ':');
    if (namespaceSeparator == NULL) return "";
    return std::string(type, static_cast<std::size_t>(namespaceSeparator-type));
  }

  std::string toLower(const std::string &value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
  }

  bool isXsdExtension(const std::filesystem::path &path) {
    std::string extension = toLower(path.extension().string());
    return extension == ".xsd";
  }

  bool parseFloat(const std::string &token, float &value) {
    const char *begin = token.c_str();
    const char *end = begin + token.size();
    return boost::spirit::x3::parse(begin, end, boost::spirit::x3::float_, value);
  }

  std::vector<float> parseFloatList(const std::string &list) {
    std::istringstream stream(list);
    std::string token;
    std::vector<float> values;
    while (stream >> token) {
      float value = 0.0f;
      if (parseFloat(token, value)) values.push_back(value);
    }
    return values;
  }

  std::string stripHashPrefix(const std::string &value) {
    if (!value.empty() && value[0] == '#') return value.substr(1);
    return value;
  }

  std::string trim(const std::string &value) {
    std::size_t start = value.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    std::size_t end = value.find_last_not_of(" \t\n\r");
    return value.substr(start, end-start+1);
  }

  std::string nodeId(const pugi::xml_node &node) {
    for (auto const &attribute: node.attributes()) {
      const char *attributeType = typeWithoutNamespace(attribute.name());
      if (strcmp(attributeType, "id") == 0) return attribute.value();
    }
    return "";
  }

  std::string resolveImageUri(const std::string &imageUri) {
    if (imageUri.empty()) return "";
    if (imageUri.find("://") != std::string::npos) return imageUri;
    if (imageUri[0] == '/') return imageUri;
    if (imageUri.size() > 1 && imageUri[1] == ':') return imageUri;
    std::filesystem::path sourcePath(currentFilePath);
    std::filesystem::path resolved = sourcePath.parent_path() / std::filesystem::path(imageUri);
    return resolved.lexically_normal().string();
  }

  std::string namespaceUriForPrefix(const std::string &prefix) const {
    auto found = namespaceUriByPrefix.find(prefix);
    if (found == namespaceUriByPrefix.end()) return "";
    return found->second;
  }

  std::string namespaceUriForNode(const pugi::xml_node &node) const {
    return namespaceUriForPrefix(namespacePrefix(node.name()));
  }

  bool isCoreNamespace(const std::string &uri) {
    if (uri.empty()) return true;
    if (uri == "http://www.opengis.net/gml" ||
        uri == "http://www.w3.org/1999/xlink" ||
        uri == "http://www.w3.org/2001/XMLSchema-instance" ||
        uri == "urn:oasis:names:tc:ciq:xsdschema:xAL:2.0" ||
        uri == "http://www.ascc.net/xml/schematron" ||
        uri == "http://www.w3.org/2001/SMIL20/" ||
        uri == "http://www.w3.org/2001/SMIL20/Language") return true;
    return uri.find("http://www.opengis.net/citygml/") == 0;
  }

  bool isBuiltInXsdType(const std::string &typeName) {
    std::string local = typeName;
    std::size_t separator = local.find(':');
    if (separator != std::string::npos) local = local.substr(separator+1);
    static const std::unordered_set<std::string> builtins = {
      "string", "boolean", "decimal", "float", "double", "duration", "dateTime", "time",
      "date", "gYearMonth", "gYear", "gMonthDay", "gDay", "gMonth", "hexBinary",
      "base64Binary", "anyURI", "QName", "NOTATION", "normalizedString", "token",
      "language", "NMTOKEN", "NMTOKENS", "Name", "NCName", "ID", "IDREF", "IDREFS",
      "ENTITY", "ENTITIES", "integer", "nonPositiveInteger", "negativeInteger", "long",
      "int", "short", "byte", "nonNegativeInteger", "unsignedLong", "unsignedInt",
      "unsignedShort", "unsignedByte", "positiveInteger"
    };
    return builtins.count(local) > 0;
  }

  bool isComplexGlobalElement(const pugi::xml_node &elementNode) {
    const char *typeAttribute = elementNode.attribute("type").value();
    if (strlen(typeAttribute) > 0) {
      return !isBuiltInXsdType(typeAttribute);
    }
    for (auto const &child: elementNode.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "complexType") == 0) return true;
    }
    return false;
  }

  void captureNamespacesFromCityModel(const pugi::xml_node &cityModelNode) {
    namespaceUriByPrefix.clear();
    for (auto const &attribute: cityModelNode.attributes()) {
      std::string attributeName = attribute.name();
      if (attributeName == "xmlns") {
        namespaceUriByPrefix[""] = attribute.value();
      } else if (attributeName.find("xmlns:") == 0 && attributeName.size() > 6) {
        namespaceUriByPrefix[attributeName.substr(6)] = attribute.value();
      }
    }
  }

  std::string readTargetNamespace(const std::filesystem::path &xsdPath) {
    pugi::xml_document xsdDocument;
    if (!xsdDocument.load_file(xsdPath.c_str())) return "";
    pugi::xml_node root = xsdDocument.document_element();
    if (root.type() == pugi::node_null) return "";
    const char *rootType = typeWithoutNamespace(root.name());
    if (strcmp(rootType, "schema") != 0) return "";
    return trim(root.attribute("targetNamespace").value());
  }

  void registerAdeClassNamesFromSchema(const std::filesystem::path &xsdPath, const std::string &namespaceUri) {
    pugi::xml_document xsdDocument;
    if (!xsdDocument.load_file(xsdPath.c_str())) return;
    pugi::xml_node root = xsdDocument.document_element();
    if (root.type() == pugi::node_null) return;
    const char *rootType = typeWithoutNamespace(root.name());
    if (strcmp(rootType, "schema") != 0) return;

    auto &classNames = adeObjectNamesByNamespace[namespaceUri];
    for (auto const &child: root.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "element") != 0) continue;
      if (!isComplexGlobalElement(child)) continue;
      std::string name = trim(child.attribute("name").value());
      if (!name.empty()) classNames.insert(name);
    }
  }

  void discoverLocalSchemasForCurrentFile() {
    xsdFilesByNamespace.clear();
    adeObjectNamesByNamespace.clear();
    matchedADENamespaces.clear();
    unmatchedADENamespaces.clear();

    std::filesystem::path sourcePath(currentFilePath);
    std::filesystem::path sourceDirectory = sourcePath.parent_path();
    if (sourceDirectory.empty()) return;

    std::error_code errorCode;
    for (const auto &entry: std::filesystem::directory_iterator(sourceDirectory, errorCode)) {
      if (errorCode) break;
      if (!entry.is_regular_file()) continue;
      if (!isXsdExtension(entry.path())) continue;
      std::string targetNamespace = readTargetNamespace(entry.path());
      if (targetNamespace.empty()) continue;
      xsdFilesByNamespace[targetNamespace].push_back(entry.path().string());
      std::cout << "Discovered XSD: " << entry.path() << " (targetNamespace: " << targetNamespace << ")" << std::endl;
    }
    if (errorCode) {
      std::cout << "Warning: failed to scan XSD files in " << sourceDirectory << ": " << errorCode.message() << std::endl;
    }

    std::unordered_set<std::string> candidateADENamespaces;
    for (const auto &namespaceEntry: namespaceUriByPrefix) {
      if (!isCoreNamespace(namespaceEntry.second)) candidateADENamespaces.insert(namespaceEntry.second);
    }
    if (candidateADENamespaces.empty()) {
      std::cout << "No ADE namespaces declared in CityModel." << std::endl;
      return;
    }

    for (const auto &namespaceUri: candidateADENamespaces) {
      auto schemas = xsdFilesByNamespace.find(namespaceUri);
      if (schemas == xsdFilesByNamespace.end()) {
        unmatchedADENamespaces.insert(namespaceUri);
        std::cout << "Warning: no local XSD found for ADE namespace " << namespaceUri << ". Fallback ADE parsing active." << std::endl;
        continue;
      }

      matchedADENamespaces.insert(namespaceUri);
      for (const auto &schemaPath: schemas->second) {
        registerAdeClassNamesFromSchema(schemaPath, namespaceUri);
      }
      std::size_t classCount = adeObjectNamesByNamespace[namespaceUri].size();
      std::cout << "Matched ADE namespace " << namespaceUri << " with " << schemas->second.size()
                << " schema file(s); registered " << classCount << " class candidates." << std::endl;
    }
  }

  bool isADECityObject(const pugi::xml_node &node) {
    const std::string namespaceUri = namespaceUriForNode(node);
    if (namespaceUri.empty()) return false;
    if (matchedADENamespaces.count(namespaceUri) == 0) return false;
    auto classNames = adeObjectNamesByNamespace.find(namespaceUri);
    if (classNames == adeObjectNamesByNamespace.end()) return false;

    const char *nodeType = typeWithoutNamespace(node.name());
    if (nodeType == nullptr || nodeType[0] == '\0') return false;
    if (classNames->second.count(nodeType) == 0) return false;

    bool hasIdentifier = !nodeId(node).empty();
    bool startsWithUppercase = std::isupper(static_cast<unsigned char>(nodeType[0])) != 0;
    bool hasChildren = std::distance(node.children().begin(), node.children().end()) > 0;
    if (!hasIdentifier && !startsWithUppercase) return false;
    if (!hasIdentifier && !hasChildren) return false;
    return true;
  }

  std::string styleKey(const AzulAppearanceStyle &style) {
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

  void assignRingTextureCoordinates(AzulRing &ring, const std::unordered_map<std::string, std::vector<std::array<float, 2>>> &ringTextureCoordinates) {
    auto found = ringTextureCoordinates.find(ring.id);
    if (found == ringTextureCoordinates.end()) return;
    const auto &parsedCoordinates = found->second;
    if (parsedCoordinates.empty() || ring.points.empty()) return;

    ring.textureCoordinates.clear();
    ring.textureCoordinates.reserve(ring.points.size());
    std::size_t targetCount = ring.points.size();
    if (parsedCoordinates.size() == targetCount) {
      ring.textureCoordinates = parsedCoordinates;
      ring.hasTextureCoordinates = true;
      return;
    }

    if (parsedCoordinates.size()+1 == targetCount && targetCount > 1) {
      ring.textureCoordinates = parsedCoordinates;
      ring.textureCoordinates.push_back(parsedCoordinates.front());
      ring.hasTextureCoordinates = true;
      return;
    }

    if (parsedCoordinates.size() == targetCount+1 && parsedCoordinates.size() > 1) {
      ring.textureCoordinates.assign(parsedCoordinates.begin(), parsedCoordinates.begin()+static_cast<long>(targetCount));
      ring.hasTextureCoordinates = true;
      return;
    }
  }

  std::string appearanceSurfaceIdForRing(const std::string &ringId) const {
    if (ringId.empty()) return "";
    auto explicitMatch = textureSurfaceIdByRingId.find(ringId);
    if (explicitMatch != textureSurfaceIdByRingId.end()) return explicitMatch->second;
    std::size_t separator = ringId.find_last_of('_');
    if (separator == std::string::npos) return "";
    std::string inferredSurfaceId = ringId.substr(0, separator);
    if (textureBySurfaceId.count(inferredSurfaceId) > 0 ||
        materialBySurfaceId.count(inferredSurfaceId) > 0) return inferredSurfaceId;
    return "";
  }

  void applyAppearancesToObject(AzulObject &parsedObject, std::vector<AzulAppearanceStyle> &sharedStyles, std::unordered_map<std::string, int> &styleIdByKey) {
    for (auto &polygon: parsedObject.polygons) {
      AzulAppearanceStyle style;
      std::unordered_map<std::string, std::vector<std::array<float, 2>>> ringTextureCoordinates;
      std::string surfaceId = polygon.id;
      if (surfaceId.empty() ||
          (textureBySurfaceId.count(surfaceId) == 0 &&
           materialBySurfaceId.count(surfaceId) == 0)) {
        std::string inferredSurfaceId = appearanceSurfaceIdForRing(polygon.exteriorRing.id);
        if (!inferredSurfaceId.empty()) surfaceId = inferredSurfaceId;
      }

      auto textureForSurface = textureBySurfaceId.find(surfaceId);
      if (textureForSurface != textureBySurfaceId.end()) {
        style.hasTexture = true;
        style.theme = textureForSurface->second.theme;
        style.textureUri = textureForSurface->second.textureUri;
        ringTextureCoordinates = textureForSurface->second.ringTextureCoordinates;
        assignRingTextureCoordinates(polygon.exteriorRing, ringTextureCoordinates);
        for (auto &ring: polygon.interiorRings) assignRingTextureCoordinates(ring, ringTextureCoordinates);
      }

      auto materialForSurface = materialBySurfaceId.find(surfaceId);
      if (materialForSurface != materialBySurfaceId.end() && materialForSurface->second.hasDiffuseColor) {
        style.hasMaterial = true;
        if (style.theme.empty()) style.theme = materialForSurface->second.theme;
        style.materialColour[0] = materialForSurface->second.diffuseColor[0];
        style.materialColour[1] = materialForSurface->second.diffuseColor[1];
        style.materialColour[2] = materialForSurface->second.diffuseColor[2];
        float transparency = materialForSurface->second.hasTransparency ? materialForSurface->second.transparency : 0.0f;
        if (transparency < 0.0f) transparency = 0.0f;
        if (transparency > 1.0f) transparency = 1.0f;
        style.materialColour[3] = 1.0f-transparency;
      }

      if (!style.hasTexture && !style.hasMaterial) continue;
      std::string key = styleKey(style);
      int styleId = -1;
      auto existing = styleIdByKey.find(key);
      if (existing == styleIdByKey.end()) {
        sharedStyles.push_back(style);
        styleId = static_cast<int>(sharedStyles.size()-1);
        styleIdByKey[key] = styleId;
      } else {
        styleId = existing->second;
      }
      polygon.appearanceStyleId = styleId;
    }
    for (auto &child: parsedObject.children) applyAppearancesToObject(child, sharedStyles, styleIdByKey);
  }

  void parseParameterizedTexture(const pugi::xml_node &node, const std::string &theme) {
    std::string imageUri;
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "imageURI") == 0) {
        imageUri = resolveImageUri(trim(child.child_value()));
        break;
      }
    }
    if (imageUri.empty()) return;

    std::unordered_map<std::string, ParsedTextureTarget> targets;
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "target") == 0) {
        std::string surfaceUri = stripHashPrefix(trim(child.attribute("uri").value()));
        if (surfaceUri.empty()) continue;
        ParsedTextureTarget target;
        target.textureUri = imageUri;
        target.theme = theme;
        for (auto const &targetChild: child.children()) {
          const char *targetChildType = typeWithoutNamespace(targetChild.name());
          if (strcmp(targetChildType, "TexCoordList") != 0) continue;
          for (auto const &coordinatesNode: targetChild.children()) {
            const char *coordinatesType = typeWithoutNamespace(coordinatesNode.name());
            if (strcmp(coordinatesType, "textureCoordinates") != 0) continue;
            std::string ringId = stripHashPrefix(trim(coordinatesNode.attribute("ring").value()));
            if (ringId.empty()) continue;
            std::vector<float> coordinateValues = parseFloatList(trim(coordinatesNode.child_value()));
            if (coordinateValues.size() < 2 || coordinateValues.size()%2 != 0) continue;
            std::vector<std::array<float, 2>> parsedCoordinates;
            parsedCoordinates.reserve(coordinateValues.size()/2);
            for (std::size_t i = 0; i < coordinateValues.size(); i += 2) {
              parsedCoordinates.push_back(std::array<float, 2>{coordinateValues[i], coordinateValues[i+1]});
            }
            target.ringTextureCoordinates[ringId] = parsedCoordinates;
          }
        }
        if (!target.textureUri.empty()) targets[surfaceUri] = target;
      }
    }

    for (const auto &target: targets) {
      textureBySurfaceId[target.first] = target.second;
      for (const auto &ringTextureCoordinates: target.second.ringTextureCoordinates) {
        textureSurfaceIdByRingId[ringTextureCoordinates.first] = target.first;
      }
    }
  }

  void parseX3DMaterial(const pugi::xml_node &node, const std::string &theme) {
    ParsedMaterial material;
    material.theme = theme;
    std::vector<std::string> targetSurfaces;
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "diffuseColor") == 0) {
        std::vector<float> values = parseFloatList(trim(child.child_value()));
        if (values.size() == 3) {
          material.hasDiffuseColor = true;
          material.diffuseColor[0] = values[0];
          material.diffuseColor[1] = values[1];
          material.diffuseColor[2] = values[2];
        }
      } else if (strcmp(childType, "transparency") == 0) {
        std::vector<float> values = parseFloatList(trim(child.child_value()));
        if (values.size() == 1) {
          material.hasTransparency = true;
          material.transparency = values[0];
        }
      } else if (strcmp(childType, "emissiveColor") == 0 ||
                 strcmp(childType, "specularColor") == 0) {
        (void)parseFloatList(trim(child.child_value()));
      } else if (strcmp(childType, "ambientIntensity") == 0 ||
                 strcmp(childType, "shininess") == 0) {
        (void)parseFloatList(trim(child.child_value()));
      } else if (strcmp(childType, "isSmooth") == 0) {
        std::string smoothValue = trim(child.child_value());
        std::transform(smoothValue.begin(), smoothValue.end(), smoothValue.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        (void)smoothValue;
      } else if (strcmp(childType, "target") == 0) {
        std::string target = stripHashPrefix(trim(child.child_value()));
        if (target.empty()) target = stripHashPrefix(trim(child.attribute("uri").value()));
        if (!target.empty()) targetSurfaces.push_back(target);
      }
    }
    if (!material.hasDiffuseColor) return;
    for (const auto &targetSurface: targetSurfaces) materialBySurfaceId[targetSurface] = material;
  }

  void parseCityGMLAppearance(const pugi::xml_node &node, const std::string &inheritedTheme) {
    const char *nodeType = typeWithoutNamespace(node.name());
    std::string activeTheme = inheritedTheme;
    if (strcmp(nodeType, "Appearance") == 0) {
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());
        if (strcmp(childType, "theme") == 0) {
          std::string parsedTheme = trim(child.child_value());
          if (!parsedTheme.empty()) {
            activeTheme = parsedTheme;
            appearanceThemes.insert(parsedTheme);
          }
        }
      }
    }
    if (strcmp(nodeType, "ParameterizedTexture") == 0) {
      parseParameterizedTexture(node, activeTheme);
      if (!activeTheme.empty()) appearanceThemes.insert(activeTheme);
      return;
    }
    if (strcmp(nodeType, "X3DMaterial") == 0) {
      parseX3DMaterial(node, activeTheme);
      if (!activeTheme.empty()) appearanceThemes.insert(activeTheme);
      return;
    }
    for (auto const &child: node.children()) parseCityGMLAppearance(child, activeTheme);
  }

  void parseCityGMLAppearances(const pugi::xml_node &node) {
    const char *nodeType = typeWithoutNamespace(node.name());
    if (strcmp(nodeType, "appearanceMember") == 0 ||
        strcmp(nodeType, "Appearance") == 0 ||
        strcmp(nodeType, "surfaceDataMember") == 0 ||
        strcmp(nodeType, "ParameterizedTexture") == 0 ||
        strcmp(nodeType, "X3DMaterial") == 0) {
      parseCityGMLAppearance(node, "");
      return;
    }
    for (auto const &child: node.children()) parseCityGMLAppearances(child);
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

  void parseRing(const pugi::xml_node &node, AzulRing &parsedRing) {
    pugi::xml_node ringNode = node.first_child();
    if (ringNode.type() == pugi::node_null) return;
    parsedRing.id = nodeId(ringNode);
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
          if (!boost::spirit::x3::parse(coordinates, last, boost::spirit::x3::float_, parsedRing.points.back().coordinates[currentCoordinate])) {
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
  }

  bool parsePoint(const pugi::xml_node &node, AzulPoint &parsedPoint) {
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "pos") == 0 ||
          strcmp(childType, "posList") == 0) {
        std::vector<float> coordinates = parseFloatList(trim(child.child_value()));
        if (coordinates.size() >= 3) {
          parsedPoint.coordinates[0] = coordinates[0];
          parsedPoint.coordinates[1] = coordinates[1];
          parsedPoint.coordinates[2] = coordinates[2];
          return true;
        }
      }
    }
    return false;
  }

  AzulObject &ensureMarkerChild(AzulObject &parentObject, const std::string &type, const std::string &id = "") {
    for (auto &child : parentObject.children) {
      if (child.type == type && child.id == id) return child;
    }
    parentObject.children.push_back(AzulObject());
    AzulObject &newChild = parentObject.children.back();
    newChild.type = type;
    newChild.id = id;
    return newChild;
  }

  void collectPointsRecursively(const pugi::xml_node &node, std::vector<AzulPoint> &points, std::unordered_map<std::string, pugi::xml_node> &nodesById, std::unordered_set<std::string> *visitedXlinks = nullptr) {
    const char *nodeType = typeWithoutNamespace(node.name());
    if (strcmp(nodeType, "Point") == 0) {
      AzulPoint point;
      if (parsePoint(node, point)) points.push_back(point);
    }

    const char *xlink = NULL;
    for (auto const &attribute: node.attributes()) {
      const char *attributeType = typeWithoutNamespace(attribute.name());
      if (strcmp(attributeType, "href") == 0) {
        xlink = attribute.value();
        break;
      }
    }
    if (xlink != NULL) {
      std::string xlinkId = stripHashPrefix(xlink);
      auto found = nodesById.find(xlinkId);
      if (found != nodesById.end()) {
        bool shouldParse = true;
        if (visitedXlinks != nullptr) {
          if (visitedXlinks->count(xlinkId)) shouldParse = false;
          else visitedXlinks->insert(xlinkId);
        }
        if (shouldParse) collectPointsRecursively(found->second, points, nodesById, visitedXlinks);
      }
    }

    for (auto const &child: node.children()) collectPointsRecursively(child, points, nodesById, visitedXlinks);
  }

  void parsePolygon(const pugi::xml_node &node, AzulPolygon &parsedPolygon) {
    parsedPolygon.id = nodeId(node);
    for (auto const &child: node.children()) {
      const char *childType = typeWithoutNamespace(child.name());
      if (strcmp(childType, "exterior") == 0) {
        parseRing(child, parsedPolygon.exteriorRing);
      } else if (strcmp(childType, "interior") == 0) {
        AzulRing ring;
        parseRing(child, ring);
        parsedPolygon.interiorRings.push_back(ring);
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
          captureNamespacesFromCityModel(node);
          discoverLocalSchemasForCurrentFile();
          std::unordered_map<std::string, pugi::xml_node> nodesById;
          std::cout << "Building nodes index...";
          buildNodesIndex(node, nodesById);
          std::cout << " done (" << nodesById.size() << " entries)." << std::endl;
          textureBySurfaceId.clear();
          materialBySurfaceId.clear();
          appearanceThemes.clear();
          parseCityGMLObject(node, parsedObject, nodesById);
          parseCityGMLAppearances(node);
          std::unordered_map<std::string, int> styleIdByKey;
          parsedObject.appearanceStyles.clear();
          parsedObject.appearanceThemes.assign(appearanceThemes.begin(), appearanceThemes.end());
          applyAppearancesToObject(parsedObject, parsedObject.appearanceStyles, styleIdByKey);
          statusMessage = "Loaded CityGML " + docVersion + " file";
          if (!unmatchedADENamespaces.empty()) {
            statusMessage += " (ADE fallback active)";
          }
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
      parsePolygon(node, polygon);
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
    const bool adeSupportedObject = isADECityObject(node);
    const std::string nodeNamespacePrefix = namespacePrefix(node.name());
//    std::cout << "Node: \"" << nodeType << "\"" << std::endl;

    // Ignored types
    if (strcmp(nodeType, "address") == 0 || // Complex type
        strcmp(nodeType, "appearance") == 0 ||  // Unsupported
        strcmp(nodeType, "Appearance") == 0 ||  // Unsupported
        strcmp(nodeType, "appearanceMember") == 0 ||  // Unsupported
        strcmp(nodeType, "surfaceDataMember") == 0 ||  // Unsupported
        strcmp(nodeType, "ParameterizedTexture") == 0 ||  // Unsupported
        strcmp(nodeType, "X3DMaterial") == 0 ||  // Unsupported
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
//        std::unordered_map<std::string, pugi::xml_node>::const_iterator xlinkNode = nodesById.find(xlink);
//        if (xlinkNode != nodesById.end()) {
//          parseCityGMLObject(xlinkNode->second, parsedObject, nodesById);
//        } else {
//          std::cout << "Geometry with xlink " << xlink << " not found. Skipped." << std::endl;
//        }
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
             strcmp(nodeType, "lod4Surface") == 0 ||
             adeSupportedObject) {

      AzulObject newChild;
      newChild.type = nodeType;
      if (adeSupportedObject && !nodeNamespacePrefix.empty()) {
        newChild.displayType = nodeNamespacePrefix + ":" + nodeType;
      }
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
      parsePolygon(node, polygon);
      parsedObject.polygons.push_back(polygon);
    }
    else if (strcmp(nodeType, "referencePoint") == 0) {
      AzulObject referencePointChild;
      referencePointChild.type = "ReferencePoint";
      referencePointChild.id = nodeId(node);
      std::unordered_set<std::string> visitedXlinks;
      collectPointsRecursively(node, referencePointChild.markerPoints, nodesById, &visitedXlinks);
      if (!referencePointChild.markerPoints.empty()) parsedObject.children.push_back(referencePointChild);
    }
    else if (strcmp(nodeType, "Point") == 0) {
      AzulPoint point;
      if (parsePoint(node, point)) {
        AzulObject &pointGeometryChild = ensureMarkerChild(parsedObject, "PointGeometry");
        pointGeometryChild.markerPoints.push_back(point);
      }
    }

    // Implicit geometry
    else if (strcmp(nodeType, "ImplicitGeometry") == 0) {
//      std::cout << "Implicit geometry" << std::endl;
      std::vector<float> transformationMatrix;
      std::vector<float> anchorPointCoordinates;

      AzulObject transformedChild;
      for (auto const &child: node.children()) {
        const char *childType = typeWithoutNamespace(child.name());

        if (strcmp(childType, "transformationMatrix") == 0) {
          const char *values = child.child_value();
          while (isspace(*values)) ++values;
          while (strlen(values) > 0) {
            const char *last = values;
            while (!isspace(*last) && *last != '\0') ++last;
            float parsedValue;
            if (!boost::spirit::x3::parse(values, last, boost::spirit::x3::float_, parsedValue)) {
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
            std::unordered_map<std::string, pugi::xml_node>::const_iterator xlinkNode = nodesById.find(xlink);
            if (xlinkNode != nodesById.end()) {
              parseCityGMLObject(xlinkNode->second, transformedChild, nodesById);
            } else {
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
                    if (!boost::spirit::x3::parse(coordinates, last, boost::spirit::x3::float_, anchorPointCoordinates.back())) {
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
            float homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
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
              float homogeneousCoordinate = (transformationMatrix[12]*point.coordinates[0] +
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

    // Fallback for namespaced extension content (ADE and similar): flatten children and scalar values.
    else if (!namespacePrefix(node.name()).empty()) {
      std::size_t numberOfChildren = std::distance(node.children().begin(), node.children().end());
      if (numberOfChildren == 0) {
        const std::string value = trim(node.child_value());
        if (!value.empty()) parsedObject.attributes.push_back(std::pair<std::string, std::string>(nodeType, value));
      } else {
        for (auto const &child: node.children()) parseCityGMLObject(child, parsedObject, nodesById);
      }
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
    parsedFile.appearanceStyles.clear();
    parsedFile.appearanceThemes.clear();
    textureBySurfaceId.clear();
    textureSurfaceIdByRingId.clear();
    materialBySurfaceId.clear();
    appearanceThemes.clear();
    namespaceUriByPrefix.clear();
    xsdFilesByNamespace.clear();
    adeObjectNamesByNamespace.clear();
    matchedADENamespaces.clear();
    unmatchedADENamespaces.clear();
    currentFilePath = filePath;
    doc.load_file(filePath);
    parseGML(doc.root(), parsedFile);
  }

  void clearDOM() {
    doc.reset();
    textureBySurfaceId.clear();
    textureSurfaceIdByRingId.clear();
    materialBySurfaceId.clear();
    appearanceThemes.clear();
    namespaceUriByPrefix.clear();
    xsdFilesByNamespace.clear();
    adeObjectNamesByNamespace.clear();
    matchedADENamespaces.clear();
    unmatchedADENamespaces.clear();
    currentFilePath.clear();
  }
};

#endif /* GMLParsingHelper_hpp */
