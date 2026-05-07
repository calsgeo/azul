import UIKit

class AttributeTableViewController: UIViewController {

    var dataManager: DataManagerWrapperWrapper!
    var selectedItem: AzulObjectIterator?

    let tableView = UITableView(frame: .zero, style: .insetGrouped)

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Attributes"
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
            preferredContentSize = CGSize(width: 360, height: 400)
        }

        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .done, target: self, action: #selector(dismissSelf))
    }

    @objc func dismissSelf() {
        dismiss(animated: true)
    }

    func showAttributes(for item: AzulObjectIterator) {
        selectedItem = item
        let ident = dataManager.identifier(ofItem: item) ?? ""
        title = ident.isEmpty
            ? (dataManager.type(ofItem: item) ?? "")
            : ident
        tableView.reloadData()
    }
}

extension AttributeTableViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        guard let item = selectedItem else { return 0 }
        return Int(dataManager.numberOfAttributes(ofItem: item))
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "cell", for: indexPath)
        guard let item = selectedItem else { return cell }

        let key = dataManager.attributeKey(ofItem: item, at: indexPath.row)
        let value = dataManager.attributeValue(ofItem: item, at: indexPath.row)

        var config = UIListContentConfiguration.subtitleCell()
        config.text = key
        config.secondaryText = value
        config.textProperties.color = .lightGray
        config.secondaryTextProperties.color = .white
        config.textProperties.font = .systemFont(ofSize: UIFont.smallSystemFontSize)
        config.secondaryTextProperties.font = .systemFont(ofSize: UIFont.smallSystemFontSize)
        cell.contentConfiguration = config
        cell.backgroundColor = UIColor(white: 0.15, alpha: 1.0)
        cell.selectionStyle = .none

        return cell
    }
}
