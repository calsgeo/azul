import UIKit
import UniformTypeIdentifiers

protocol ObjectListViewControllerDelegate: AnyObject {
    func objectListDidSelectItem(_ item: AzulObjectIterator)
}

class ObjectListViewController: UIViewController {

    weak var delegate: ObjectListViewControllerDelegate?
    var dataManager: DataManagerWrapperWrapper!
    var flatItems: [AzulObjectIterator] = []
    var expandedItems = Set<AzulObjectIterator>()

    let tableView = UITableView(frame: .zero, style: .plain)

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Objects"
        overrideUserInterfaceStyle = .dark
        view.backgroundColor = .systemBackground

        tableView.delegate = self
        tableView.dataSource = self
        tableView.register(UITableViewCell.self, forCellReuseIdentifier: "cell")
        tableView.separatorColor = UIColor(white: 0.25, alpha: 1.0)
        tableView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(tableView)

        NSLayoutConstraint.activate([
            tableView.topAnchor.constraint(equalTo: view.topAnchor),
            tableView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            tableView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        if UIDevice.current.userInterfaceIdiom == .pad {
            preferredContentSize = CGSize(width: 400, height: 600)
        }

        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .done, target: self, action: #selector(dismissSelf))

        rebuildFlatItems()
    }

    @objc func dismissSelf() {
        dismiss(animated: true)
    }

    func rebuildFlatItems() {
        flatItems.removeAll()
        let fileCount = dataManager.numberOfParsedFiles()
        for i in 0..<fileCount {
            let file = dataManager.iteratorForFile(at: i) as! AzulObjectIterator
            appendFlattened(file)
        }
        tableView.reloadData()
    }

    private func appendFlattened(_ item: AzulObjectIterator) {
        flatItems.append(item)
        if expandedItems.contains(item), dataManager.isItemExpandable(item) {
            let childCount = dataManager.numberOfChildren(ofItem: item)
            for i in 0..<childCount {
                let child = dataManager.child(ofItem: item, at: i) as! AzulObjectIterator
                child.depth = item.depth + 1
                appendFlattened(child)
            }
        }
    }

    func toggleExpandItem(_ item: AzulObjectIterator) {
        if expandedItems.contains(item) {
            expandedItems.remove(item)
        } else {
            expandedItems.insert(item)
        }
        rebuildFlatItems()
    }
}

extension ObjectListViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        flatItems.count
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "cell", for: indexPath)
        let item = flatItems[indexPath.row]

        let typeName = dataManager.type(ofItem: item) ?? ""
        let identifier = dataManager.identifier(ofItem: item) ?? ""
        let hasChildren = dataManager.isItemExpandable(item)
        let visible = dataManager.visibleState(ofItem: item)

        let isFile = item.depth == 0
        if isFile {
            let fileExtension = (identifier as NSString).pathExtension
            cell.imageView?.image = UIImage(systemName: sfSymbolForFileExtension(fileExtension))
            cell.imageView?.tintColor = .systemGray
        } else {
            if let icon = UIImage(named: typeName) {
                cell.imageView?.image = icon
            } else {
                cell.imageView?.image = UIImage(systemName: "cube.transparent")
                cell.imageView?.tintColor = .systemGray
            }
        }

        var displayText = typeName
        if !identifier.isEmpty {
            displayText += " — \(identifier)"
        }
        cell.textLabel?.text = displayText
        cell.textLabel?.font = .systemFont(ofSize: UIFont.smallSystemFontSize)
        cell.textLabel?.textColor = .white

        if hasChildren {
            cell.accessoryType = .disclosureIndicator
            cell.accessoryView = nil
        } else {
            cell.accessoryType = .none
            let switchView = UISwitch()
            switchView.isOn = visible != 78 // 'N'
            switchView.addTarget(self, action: #selector(visibilityToggled(_:)), for: .valueChanged)
            cell.accessoryView = switchView
        }

        cell.backgroundColor = UIColor(white: 0.12, alpha: 1.0)
        cell.indentationLevel = Int(item.depth)
        cell.indentationWidth = 16
        cell.selectionStyle = .default

        return cell
    }

    @objc func visibilityToggled(_ sender: UISwitch) {
        let point = sender.convert(CGPoint.zero, to: tableView)
        guard let indexPath = tableView.indexPathForRow(at: point),
              indexPath.row < flatItems.count else { return }
        let item = flatItems[indexPath.row]
        let newState: Int8 = sender.isOn ? 89 : 78 // 'Y' : 'N'
        dataManager.setVisibleState(newState, forItem: item)
    }

    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        let item = flatItems[indexPath.row]

        if dataManager.isItemExpandable(item) {
            toggleExpandItem(item)
        } else {
            delegate?.objectListDidSelectItem(item)
        }
    }
}

private func sfSymbolForFileExtension(_ ext: String) -> String {
    switch ext.lowercased() {
    case "json", "jsonl": return "curlybraces"
    case "xml", "gml": return "doc.richtext"
    case "obj": return "cube"
    case "off", "poly": return "shapes"
    case "azulview": return "gearshape.2"
    default: return "doc"
    }
}
