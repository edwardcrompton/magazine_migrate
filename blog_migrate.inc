<?php
/**
 * @file
 * Magazine migration classes.
 * 
 */


/**
 * Class holding common settings for article migration.
 *
 * @see migrate.drush.inc for migrate drush commands
 */
class LinksMigration extends Migration {

  /**
   *
   *
   */
  public function __parentConstruct() {
    parent::__construct();
  }

  public function __construct() {
    parent::__construct(MigrateGroup::getInstance('blog_migrate'));

    $query = $this->getArticlesQuery();
    $this->source = new MigrateSourceSQL($query);

    $this->destination = new MigrateDestinationNode('page');

    // We instantiate the MigrateMap
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'id' => array(
          'type' => 'int',
          'not null' => TRUE,
          'description' => 'Article Id',
          'alias' => 'u',
        )
      ),
      MigrateDestinationNode::getKeySchema()
    );

    $this->addFieldMapping('title', 'title')
      ->description(t('Mapping headline in source to field title'));
    $this->addFieldMapping('body', 'url')
      ->description(t('Mapping the url in source to the body'));
  }

  /**
  * Called before any mapping is done
  * @param unknown_type $row
  */
  public function prepareRow($row) {
    
  }

  /**
   * Sets various values before the node is inserted
   *
   */
  public function prepare(stdClass $node, stdClass $row) {

  }

  /**
   * Called after the node has been saved
   * @param unknown_type $entity
   * @param unknown_type $row
   */
  public function complete(stdClass $entity, stdClass $row) {
    
  }

  /**
   * 
   */
  public function getArticlesQuery(){
    $query = Database::getConnection('default', 'source')
      ->select('urls', 'u')
      ->fields('u', array('id', 'title', 'url'));
    return $query;
  }
}
