require("config")

local saveCodec = {}

saveCodec.libraryMissingMessage = "No input/output library selected for CommunityMP save codec!"
saveCodec.formatName = "CommunityMP XML Save"
saveCodec.schemaVersion = 1

function saveCodec.setLibrary(ioLibrary)
    saveCodec.ioLibrary = ioLibrary
end

function saveCodec.setFileOps(fileOps)
    saveCodec.fileOps = fileOps
end

local function getLibrary()
    if saveCodec.ioLibrary ~= nil then
        return saveCodec.ioLibrary
    end

    if tes3mp.GetOperatingSystemType ~= nil and tes3mp.GetOperatingSystemType() == "Windows" then
        local status, io2 = pcall(require, "io2")
        if status then
            saveCodec.ioLibrary = io2
            return io2
        end
    end

    saveCodec.ioLibrary = io
    return io
end

local function escapeXml(value)
    value = tostring(value)
    value = value:gsub("&", "&amp;")
    value = value:gsub("<", "&lt;")
    value = value:gsub(">", "&gt;")
    value = value:gsub("\"", "&quot;")
    value = value:gsub("'", "&apos;")
    return value
end

local function unescapeXml(value)
    value = tostring(value)
    value = value:gsub("&apos;", "'")
    value = value:gsub("&quot;", "\"")
    value = value:gsub("&gt;", ">")
    value = value:gsub("&lt;", "<")
    value = value:gsub("&amp;", "&")
    return value
end

local function sortKeys(inputTable)
    local keys = {}

    for key in pairs(inputTable) do
        table.insert(keys, key)
    end

    table.sort(keys, function(left, right)
        local leftType = type(left)
        local rightType = type(right)

        if leftType == rightType then
            if leftType == "number" then
                return left < right
            end

            return tostring(left) < tostring(right)
        end

        if leftType == "number" then
            return true
        elseif rightType == "number" then
            return false
        end

        return leftType < rightType
    end)

    return keys
end

local function encodeAttributes(attributes)
    local output = ""

    for _, key in ipairs(sortKeys(attributes)) do
        local value = attributes[key]

        if value ~= nil then
            output = output .. " " .. tostring(key) .. "=\"" .. escapeXml(value) .. "\""
        end
    end

    return output
end

local function appendNode(output, key, value, indentation)
    local valueType = type(value)

    if valueType == "nil" or valueType == "function" or valueType == "userdata" or valueType == "thread" then
        return
    end

    local keyType = type(key)
    local indent = string.rep("    ", indentation)
    local attributes = " key=\"" .. escapeXml(key) .. "\" keyType=\"" .. keyType .. "\" type=\"" .. valueType .. "\""

    if valueType == "table" then
        table.insert(output, indent .. "<node" .. attributes .. ">\n")

        for _, childKey in ipairs(sortKeys(value)) do
            appendNode(output, childKey, value[childKey], indentation + 1)
        end

        table.insert(output, indent .. "</node>\n")
        return
    end

    local text = ""
    if valueType == "boolean" then
        text = value and "true" or "false"
    else
        text = escapeXml(value)
    end

    table.insert(output, indent .. "<node" .. attributes .. ">" .. text .. "</node>\n")
end

local function encodeNode(key, value, indentation)
    local output = {}
    appendNode(output, key, value, indentation)
    return table.concat(output)
end

function saveCodec.encode(kind, data, metadata)
    local attributes = {
        format = saveCodec.formatName,
        kind = kind or "data",
        savedAt = os.time(),
        schemaVersion = saveCodec.schemaVersion
    }

    if type(metadata) == "table" then
        for key, value in pairs(metadata) do
            attributes[key] = value
        end
    end

    local output = {
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n",
        "<save" .. encodeAttributes(attributes) .. ">\n"
    }

    appendNode(output, "data", data or {}, 1)
    table.insert(output, "</save>\n")

    return table.concat(output)
end

local function parseAttributes(tag)
    local attributes = {}

    for key, value in tag:gmatch("([%w_:-]+)%s*=%s*\"(.-)\"") do
        attributes[key] = unescapeXml(value)
    end

    return attributes
end

local function findMatchingNodeClose(content, searchStart)
    local depth = 1
    local searchPosition = searchStart

    while true do
        local nextOpen = content:find("<node%s", searchPosition)
        local nextClose = content:find("</node>", searchPosition, true)

        if nextClose == nil then
            return nil, nil
        end

        if nextOpen ~= nil and nextOpen < nextClose then
            local openEnd = content:find(">", nextOpen, true)
            if openEnd == nil then
                return nil, nil
            end

            local openTag = content:sub(nextOpen, openEnd)
            if not openTag:match("/%s*>$") then
                depth = depth + 1
            end

            searchPosition = openEnd + 1
        else
            depth = depth - 1
            if depth == 0 then
                return nextClose, nextClose + string.len("</node>")
            end

            searchPosition = nextClose + string.len("</node>")
        end
    end
end

local parseNode

local function decodeScalar(valueType, text)
    if valueType == "number" then
        return tonumber(text) or 0
    elseif valueType == "boolean" then
        return text == "true"
    elseif valueType == "string" then
        return unescapeXml(text)
    elseif valueType == "table" then
        return {}
    end

    return unescapeXml(text)
end

local function decodeKey(attributes)
    local key = attributes.key or ""

    if attributes.keyType == "number" then
        return tonumber(key)
    elseif attributes.keyType == "boolean" then
        return key == "true"
    end

    return key
end

parseNode = function(content, startPosition)
    local openStart, openEnd = content:find("<node%s.-%/?>", startPosition)

    if openStart == nil then
        return nil, startPosition
    end

    local openTag = content:sub(openStart, openEnd)
    local attributes = parseAttributes(openTag)
    local selfClosing = openTag:match("/%s*>$") ~= nil
    local valueType = attributes.type or "string"
    local node = {
        key = decodeKey(attributes),
        value = nil
    }

    if selfClosing then
        node.value = decodeScalar(valueType, "")
        return node, openEnd + 1
    end

    local closeStart, closeEnd = findMatchingNodeClose(content, openEnd + 1)
    if closeStart == nil then
        return nil, openEnd + 1
    end

    local inner = content:sub(openEnd + 1, closeStart - 1)

    if valueType == "table" then
        local output = {}
        local childPosition = 1

        while true do
            local childStart = inner:find("<node%s", childPosition)
            if childStart == nil then
                break
            end

            local childNode, nextPosition = parseNode(inner, childStart)
            if childNode == nil then
                break
            end

            output[childNode.key] = childNode.value
            childPosition = nextPosition
        end

        node.value = output
    else
        node.value = decodeScalar(valueType, inner)
    end

    return node, closeEnd
end

function saveCodec.decode(content)
    local document = saveCodec.decodeDocument(content)
    if document == nil then
        return nil
    end

    return document.data
end

function saveCodec.decodeDocument(content)
    if content == nil or content == "" then
        return nil
    end

    local saveOpenStart, saveOpenEnd = content:find("<save[%s>].->")
    if saveOpenStart == nil then
        return nil
    end

    if content:match("</save>%s*$") == nil then
        return nil
    end

    local saveCloseStart = content:find("</save>", saveOpenEnd + 1, true)
    if saveCloseStart == nil then
        return nil
    end

    local attributes = parseAttributes(content:sub(saveOpenStart, saveOpenEnd))
    if attributes.format ~= nil and attributes.format ~= saveCodec.formatName then
        return nil
    end

    local node = parseNode(content:sub(saveOpenEnd + 1, saveCloseStart - 1), 1)
    if node == nil then
        return nil
    end

    if node.key ~= "data" then
        return nil
    end

    return {
        attributes = attributes,
        data = node.value
    }
end

local function normalizeRelativePath(relativePath)
    if type(relativePath) ~= "string" then
        return nil
    end

    if relativePath == "" then
        return ""
    end

    local path = relativePath:gsub("\\", "/")

    if path:find("%z") ~= nil or path:match("^/") or path:match("^%a:") or
        path:find("//", 1, true) ~= nil or path:sub(-1) == "/" then
        return nil
    end

    for segment in path:gmatch("[^/]+") do
        if segment == "" or segment == "." or segment == ".." then
            return nil
        end
    end

    return path
end

function saveCodec.normalizeRelativePath(relativePath)
    return normalizeRelativePath(relativePath)
end

function saveCodec.isSafeRelativePath(relativePath)
    return normalizeRelativePath(relativePath) ~= nil
end

local function getFullPath(relativePath)
    local safeRelativePath = normalizeRelativePath(relativePath)

    if safeRelativePath == nil then
        return nil
    end

    return config.dataPath .. "/" .. safeRelativePath
end

local function getDirectory(relativePath)
    return relativePath:match("^(.*)/[^/]*$") or ""
end

local function quotePath(path)
    return "\"" .. tostring(path):gsub("\"", "\\\"") .. "\""
end

local function getFileOps()
    local fileOps = saveCodec.fileOps or {}

    return {
        remove = fileOps.remove or os.remove,
        rename = fileOps.rename or os.rename
    }
end

local function callFileOp(fileOp, ...)
    local status, result = pcall(fileOp, ...)
    return status and result == true
end

local function removeFile(relativePath)
    local fullPath = getFullPath(relativePath)

    if fullPath ~= nil then
        callFileOp(getFileOps().remove, fullPath)
    end
end

local function moveFile(sourcePath, destinationPath)
    local fullSourcePath = getFullPath(sourcePath)
    local fullDestinationPath = getFullPath(destinationPath)

    if fullSourcePath == nil or fullDestinationPath == nil then
        return false
    end

    return callFileOp(getFileOps().rename, fullSourcePath, fullDestinationPath)
end

function saveCodec.ensureDirectory(relativePath)
    if relativePath == nil or relativePath == "" then
        return true
    end

    local safeRelativePath = normalizeRelativePath(relativePath)
    if safeRelativePath == nil then
        return false
    end

    local fullPath = getFullPath(safeRelativePath)

    if tes3mp.DoesFilePathExist ~= nil and tes3mp.DoesFilePathExist(fullPath) then
        return true
    end

    local operatingSystemType = tes3mp.GetOperatingSystemType ~= nil and tes3mp.GetOperatingSystemType() or nil
    local isWindows = operatingSystemType == "Windows"

    if operatingSystemType == nil and package ~= nil and type(package.config) == "string" then
        isWindows = package.config:sub(1, 1) == "\\"
    end

    if isWindows then
        local windowsPath = fullPath:gsub("/", "\\")
        os.execute("if not exist " .. quotePath(windowsPath) .. " mkdir " .. quotePath(windowsPath))
    else
        os.execute("mkdir -p " .. quotePath(fullPath))
    end

    return true
end

function saveCodec.exists(relativePath)
    local fullPath = getFullPath(relativePath)
    if fullPath == nil then
        return false
    end

    if tes3mp.DoesFilePathExist ~= nil then
        return tes3mp.DoesFilePathExist(fullPath)
    end

    local library = getLibrary()
    local file = library.open(fullPath, "r")
    if file == nil then
        return false
    end

    file:close()
    return true
end

function saveCodec.readFromFile(relativePath)
    local library = getLibrary()
    local fullPath = getFullPath(relativePath)

    if fullPath == nil then
        return nil
    end

    if library == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, saveCodec.libraryMissingMessage)
        return nil
    end

    local file = library.open(fullPath, "r")
    if file == nil then
        return nil
    end

    local content = file:read("*all")
    file:close()

    return content
end

local function writeDirect(relativePath, content)
    local library = getLibrary()
    local fullPath = getFullPath(relativePath)

    if fullPath == nil then
        return false
    end

    if library == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, saveCodec.libraryMissingMessage)
        return false
    end

    saveCodec.ensureDirectory(getDirectory(relativePath))

    local file = library.open(fullPath, "w+b")
    if file == nil then
        return false
    end

    file:write(content)
    file:close()
    return true
end

function saveCodec.writeToFile(relativePath, content)
    local tempPath = relativePath .. ".tmp"
    local backupPath = relativePath .. ".bak"

    if not writeDirect(tempPath, content) then
        return false
    end

    local hadExistingFile = saveCodec.exists(relativePath)
    local movedExistingFile = false

    if hadExistingFile then
        removeFile(backupPath)
        movedExistingFile = moveFile(relativePath, backupPath)

        if not movedExistingFile then
            local oldContent = saveCodec.readFromFile(relativePath)
            if oldContent ~= nil then
                writeDirect(backupPath, oldContent)
            end
        end
    end

    if moveFile(tempPath, relativePath) then
        return true
    end

    local fallbackSaved = writeDirect(relativePath, content)
    removeFile(tempPath)

    if fallbackSaved then
        return true
    end

    if movedExistingFile then
        moveFile(backupPath, relativePath)
    end

    return false
end

function saveCodec.save(relativePath, kind, data, metadata)
    return saveCodec.writeToFile(relativePath, saveCodec.encode(kind, data, metadata))
end

function saveCodec.quicksave(relativePath, kind, data, metadata)
    return saveCodec.save(relativePath, kind, data, metadata)
end

local function loadDocumentWithoutRecovery(relativePath)
    local content = saveCodec.readFromFile(relativePath)

    if content == nil then
        return nil, "missing"
    end

    local document = saveCodec.decodeDocument(content)

    if document == nil then
        return nil, "invalid"
    end

    return document, nil
end

function saveCodec.loadDocument(relativePath)
    local document, failureReason = loadDocumentWithoutRecovery(relativePath)

    if document ~= nil then
        return document
    end

    local backupPath = relativePath .. ".bak"
    local backupDocument = loadDocumentWithoutRecovery(backupPath)

    if backupDocument ~= nil then
        backupDocument.recoveredFromBackup = true
        tes3mp.LogMessage(enumerations.log.WARN,
            "Recovered XML save " .. relativePath .. " from backup after " .. failureReason .. " primary file")
        return backupDocument
    end

    if failureReason == "invalid" then
        tes3mp.LogMessage(enumerations.log.ERROR, "XML save " .. relativePath .. " is corrupt and no backup could be loaded")
    end

    return nil
end

function saveCodec.load(relativePath)
    local document = saveCodec.loadDocument(relativePath)

    if document == nil then
        return nil
    end

    return document.data
end

return saveCodec
