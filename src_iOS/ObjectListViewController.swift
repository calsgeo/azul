import UIKit

protocol ObjectListViewControllerDelegate: AnyObject {
    func objectListDidSelectItem(_ item: AzulObjectIterator)
}

class ObjectListViewController: UIViewController {

    weak var delegate: ObjectListViewControllerDelegate?
    var dataManager: DataManagerWrapperWrapper!
    var visibleRows: [AzulObjectIterator] = []
    var expandedItems = Set<AnyHashable>()

    let tableView = UITableView(frame: .zero, style: .insetGrouped)

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Objects"
        view.backgroundColor = .systemBackground

        tableView.delegate = self
        tableView.dataSource = self
        tableView.register(UITableViewCell.self, forCellReuseIdentifier: "cell")
        tableView.indicatorStyle = .white
        tableView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(tableView)

        NSLayoutConstraint.activate([
            tableView.topAnchor.constraint(equalTo: view.topAnchor),
            tableView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            tableView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        if UIDevice.current.userInterfaceIdiom == .pad {
            preferredContentSize = CGSize(width: 360, height: 600)
        }

        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .done, target: self, action: #selector(dismissSelf))

        rebuildVisibleRows()
    }

    @objc func dismissSelf() {
        dismiss(animated: true)
    }

    func rebuildVisibleRows() {
        visibleRows.removeAll()
        let fileCount = dataManager.numberOfParsedFiles()
        for i in 0..<fileCount {
            let file = dataManager.iteratorForFile(at: i) as! AzulObjectIterator
            appendItem(file)
        }
        tableView.reloadData()
    }

    func appendItem(_ item: AzulObjectIterator) {
        visibleRows.append(item)
        if expandedItems.contains(item), dataManager.isItemExpandable(item) {
            let childCount = dataManager.numberOfChildren(ofItem: item)
            for i in 0..<childCount {
                let child = dataManager.child(ofItem: item, at: i) as! AzulObjectIterator
                child.depth = item.depth + 1
                appendItem(child)
            }
        }
    }

    func toggleExpandItem(_ item: AzulObjectIterator) {
        if expandedItems.contains(item) {
            expandedItems.remove(item)
        } else {
            expandedItems.insert(item)
        }
        rebuildVisibleRows()
    }
}

extension ObjectListViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        visibleRows.count
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "cell", for: indexPath)
        let item = visibleRows[indexPath.row]

        let typeName = dataManager.type(ofItem: item) ?? ""
        let identifier = dataManager.identifier(ofItem: item) ?? ""
        let hasChildren = dataManager.isItemExpandable(item)
        let visible = dataManager.visibleState(ofItem: item)

        var displayText = typeName
        if !identifier.isEmpty {
            displayText += " — \(identifier)"
        }
        cell.textLabel?.text = displayText
        cell.textLabel?.font = .systemFont(ofSize: UIFont.smallSystemFontSize)
        cell.textLabel?.textColor = .white

        if hasChildren {
            let isExpanded = expandedItems.contains(item)
            let chevron = isExpanded ? "chevron.down" : "chevron.right"
            let config = UIImage.SymbolConfiguration(pointSize: 12, weight: .medium)
            let image = UIImage(systemName: chevron, withConfiguration: config)?
                .withRenderingMode(.alwaysTemplate)
            let chevronView = UIImageView(image: image)
            chevronView.tintColor = .systemGray
            cell.accessoryView = chevronView
        } else {
            let switchView = UISwitch()
            switchView.isOn = visible != 78 // 'N'
            switchView.tag = indexPath.row
            switchView.addTarget(self, action: #selector(visibilityToggled(_:)), for: .valueChanged)
            cell.accessoryView = switchView
        }

        cell.backgroundColor = UIColor(white: 0.15, alpha: 1.0)
        cell.indentationLevel = Int(item.depth)
        cell.indentationWidth = 16
        cell.selectionStyle = .default

        return cell
    }

    @objc func visibilityToggled(_ sender: UISwitch) {
        let row = sender.tag
        guard row < visibleRows.count else { return }
        let item = visibleRows[row]
        let newState: Int8 = sender.isOn ? 89 : 78 // 'Y' : 'N'
        dataManager.setVisibleState(newState, forItem: item)
    }

    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        let item = visibleRows[indexPath.row]

        if dataManager.isItemExpandable(item) {
            toggleExpandItem(item)
        } else {
            delegate?.objectListDidSelectItem(item)
        }
    }
}
